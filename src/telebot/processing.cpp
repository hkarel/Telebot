#include "processing.h"

#include "trigger.h"
#include "group_chat.h"

#include "shared/break_point.h"
#include "shared/utils.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"
#include "shared/config/appl_conf.h"

#include <QTextCodec>

#define log_error_m   alog::logger().error  (alog_line_location, "Processing")
#define log_warn_m    alog::logger().warn   (alog_line_location, "Processing")
#define log_info_m    alog::logger().info   (alog_line_location, "Processing")
#define log_verbose_m alog::logger().verbose(alog_line_location, "Processing")
#define log_debug_m   alog::logger().debug  (alog_line_location, "Processing")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "Processing")

namespace tbot {

using namespace std;

QMutex Processing::_threadLock;
QWaitCondition Processing::_threadCond;
QList<QByteArray> Processing::_updates;

Processing::Processing()
{}

bool Processing::init(qint64 botUserId)
{
    _botUserId = botUserId;
    _configChanged = true;
    return true;
}

void Processing::addUpdate(const QByteArray& data)
{
    QMutexLocker locker {&_threadLock}; (void) locker;

    //log_debug_m << "Webhook event data: " << data;

    _updates.append(data);
    _threadCond.wakeOne();
}

void Processing::configChanged()
{
    _configChanged = true;
}

void Processing::run()
{
    log_info_m << "Started";

    while (true)
    {
        CHECK_QTHREADEX_STOP
        QByteArray data;

        if (_configChanged)
        {
            _spamIsActive = false;
            config::base().getValue("bot.spam_message.active", _spamIsActive);

            _spamMessage.clear();
            config::base().getValue("bot.spam_message.text", _spamMessage);

            _configChanged = false;
        }

        { //Block for QMutexLocker
            QMutexLocker locker {&_threadLock}; (void) locker;

            if (_updates.isEmpty())
                _threadCond.wait(&_threadLock, 50);

            if (_updates.isEmpty())
                continue;

            data = _updates.takeFirst();
        }
        if (data.isEmpty())
            continue;

        Update update;
        if (!update.fromJson(data))
            continue;

        Message::Ptr message = update.message;
        if (message.empty())
        {
            message = update.edited_message;
        }
        if (message.empty())
        {
            log_error_m << log_format(R"("update_id":%?. Message struct is empty)",
                                      update.update_id);
            continue;
        }
        if (message->chat.empty())
        {
            log_error_m << log_format(R"("update_id":%?. Chat struct is empty)",
                                      update.update_id);
            continue;
        }
        if (message->from.empty())
        {
            log_error_m << log_format(R"("update_id":%?. User struct is empty)",
                                      update.update_id);
            continue;
        }

        qint64 chatId = message->chat->id;
        qint64 userId = message->from->id;
        qint32 messageId = message->message_id;

        // Удаляем служебные сообщения Телеграм от имени бота
        if (_botUserId == userId)
        {
            HttpParams params;
            params["chat_id"] = chatId;
            params["message_id"] = messageId;
            emit sendTgCommand("deleteMessage", params);
            continue;
        }

        GroupChat::List chats = tbot::groupChats();

        lst::FindResult fr = chats.findRef(chatId);
        if (fr.failed())
        {
            log_warn_m << log_format("Group chat %? not belong to list chats"
                                     " in config. It skipped", chatId);

            if (_spamIsActive && !_spamMessage.isEmpty())
            {
                tbot::HttpParams params;
                params["chat_id"] = chatId;
                params["text"] = _spamMessage;
                emit sendTgCommand("sendMessage", params);
            }
            continue;
        }

        QString clearText = message->text;
        for (const MessageEntity& entity : message->entities)
        {
            if (entity.type == "url")
                clearText.remove(entity.offset, entity.length);
        }
        clearText = clearText.trimmed();

        if (!message->caption.isEmpty())
        {
            if (clearText.isEmpty())
                clearText = message->caption;
            else
                clearText = message->caption + '\n' + clearText;
        }

        GroupChat* chat = chats.item(fr.index());
        QSet<qint64> adminIds = chat->adminIds();

        log_verbose_m << log_format(R"("update_id":%?. Chat: %?. Clear text: %?)",
                                    update.update_id, chat->name(), clearText);

        // Проверка пользователя на принадлежность к списку администраторов
        if (chat->skipAdmins && adminIds.contains(userId))
        {
            log_verbose_m << log_format(
                R"("update_id":%?. Chat: %?. Triggers skipped, user @%? is admin)",
                update.update_id, chat->name(), message->from->username);
            continue;
        }

        // Проверка пользователя на принадлежность к белому списку
        if (chat->whiteUsers.contains(userId))
        {
            log_verbose_m << log_format(
                R"("update_id":%?. Chat: %?. Triggers skipped, user @%? in whitelist of group)",
                update.update_id, chat->name(), message->from->username);
            continue;
        }

        for (tbot::Trigger* trigger : chat->triggers)
        {
            // Проверка пользователя на принадлежность к списку администраторов
            if (trigger->skipAdmins && adminIds.contains(userId))
            {
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Trigger '%?' skipped, user @%? is admin)",
                    update.update_id, chat->name(), trigger->name, message->from->username);
                continue;
            }

            // Проверка пользователя на принадлежность к белому списку
            if (trigger->whiteUsers.contains(userId))
            {
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Trigger '%?' skipped, user @%? in whitelist of trigger)",
                    update.update_id, chat->name(), trigger->name, message->from->username);
                continue;
            }

            if (trigger->isActive(update, chat, clearText))
            {
                // Если триггер активирован - удаляем сообщение
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Delete message (chat_id: %?; message_id: %?))",
                    update.update_id, chat->name(), chatId, messageId);

                HttpParams params;
                params["chat_id"] = chatId;
                params["message_id"] = messageId;
                emit sendTgCommand("deleteMessage", params);

                // Отправляем отчет о спаме
                emit reportSpam(chatId, message->from);
                break;
            }
        }
    } // while (true)

    log_info_m << "Stopped";
}

} // namespace tbot
