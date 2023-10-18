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
QMap<QString, Processing::MediaGroup> Processing::_mediaGroups;

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

void Processing::reloadConfig()
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

        // Не обрабатываем сообщения о вступлении в группу новых участников
        if (message && message->new_chat_members.count())
            continue;

        // Не обрабатываем сообщения о покинувших группу участниках
        if (message && message->left_chat_member)
            continue;

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

        if (!message->media_group_id.isEmpty())
        {
            QMutexLocker locker {&_threadLock}; (void) locker;
            MediaGroup& mg = _mediaGroups[message->media_group_id];
            if (mg.isBad)
            {
                HttpParams params;
                params["chat_id"] = chatId;
                params["message_id"] = messageId;
                emit sendTgCommand("deleteMessage", params);
                continue;
            }
            else
            {
                if (mg.chatId == 0)
                    mg.chatId = chatId;

                if (mg.chatId != chatId)
                {
                    break_point
                    log_error_m << "Failed chat id for media group";
                }
                mg.messageIds.insert(messageId);
            }
        }

        QString clearText = message->text;
        for (int i = message->entities.count() - 1; i >= 0; --i)
        {
            const MessageEntity& entity = message->entities[i];
            if (entity.type == "url")
                clearText.remove(entity.offset, entity.length);
        }
        clearText = clearText.trimmed();

        QString clearCaption = message->caption;
        for (int i = message->caption_entities.count() - 1; i >= 0; --i)
        {
            const MessageEntity& entity = message->caption_entities[i];
            if (entity.type == "url")
                clearCaption.remove(entity.offset, entity.length);
        }
        clearCaption = clearCaption.trimmed();

        if (!clearCaption.isEmpty())
        {
            if (clearText.isEmpty())
                clearText = clearCaption;
            else
                clearText = clearCaption + '\n' + clearText;
        }

        QString usernameText = QString("%1 %2 %3")
                                    .arg(message->from->first_name)
                                    .arg(message->from->last_name)
                                    .arg(message->from->username).trimmed();

        if (message->forward_from)
        {
            usernameText += QString(" %1 %2 %3")
                                    .arg(message->forward_from->first_name)
                                    .arg(message->forward_from->last_name)
                                    .arg(message->forward_from->username);
            usernameText = usernameText.trimmed();
        }

        if (message->forward_from_chat)
        {
            usernameText += QString(" %1 %2 %3 %4")
                                    .arg(message->forward_from_chat->title)
                                    .arg(message->forward_from_chat->first_name)
                                    .arg(message->forward_from_chat->last_name)
                                    .arg(message->forward_from_chat->username);
            usernameText = usernameText.trimmed();
        }

        if (message->via_bot)
        {
            usernameText += QString(" %1 %2 %3")
                                    .arg(message->via_bot->first_name)
                                    .arg(message->via_bot->last_name)
                                    .arg(message->via_bot->username);
            usernameText = usernameText.trimmed();
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
                R"("update_id":%?. Chat: %?. Triggers skipped, user @%? in group whitelist)",
                update.update_id, chat->name(), message->from->username);
            continue;
        }

        tbot::Trigger::Text triggerText;
        triggerText[tbot::Trigger::TextType::Content]  = clearText;
        triggerText[tbot::Trigger::TextType::UserName] = usernameText;
        triggerText[tbot::Trigger::TextType::FileMime] = "";

        if (message->document)
        {
            triggerText[tbot::Trigger::TextType::FileMime] =
                QString("%1 %2").arg(message->document->file_name)
                                .arg(message->document->mime_type).trimmed();
        }

        for (tbot::Trigger* trigger : chat->triggers)
        {
            if (!trigger->active)
            {
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Trigger '%?' skipped, it not active)",
                    update.update_id, chat->name(), trigger->name);
                continue;
            }

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
                    R"("update_id":%?. Chat: %?. Trigger '%?' skipped, user @%? in trigger whitelist)",
                    update.update_id, chat->name(), trigger->name, message->from->username);
                continue;
            }

            bool triggerActive = trigger->isActive(update, chat, triggerText);

            if (trigger->inverse)
                triggerActive = !triggerActive;

            if (triggerActive)
            {
                // Если триггер активирован - удаляем сообщение
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Delete message (chat_id: %?; message_id: %?))",
                    update.update_id, chat->name(), chatId, messageId);

                if (!message->media_group_id.isEmpty())
                {
                    QMutexLocker locker {&_threadLock}; (void) locker;
                    MediaGroup& mg = _mediaGroups[message->media_group_id];
                    mg.isBad = true;
                    for (qint64 msgId : mg.messageIds)
                    {
                        HttpParams params;
                        params["chat_id"] = mg.chatId;
                        params["message_id"] = msgId;
                        emit sendTgCommand("deleteMessage", params);
                    }
                    mg.messageIds.clear();
                }
                else
                {
                    HttpParams params;
                    params["chat_id"] = chatId;
                    params["message_id"] = messageId;
                    emit sendTgCommand("deleteMessage", params);
                }

                // Отправляем в Телеграм сообщение с описанием причины удаления сообщения
                QString botMsh =
                    u8"Бот удалил сообщение"
                    u8"\r\n---"
                    u8"\r\n%1"
                    u8"\r\n---"
                    u8"\r\nПричина удаления сообщения"
                    u8"\r\n%2;"
                    u8"\r\nтриггер: %3";

                QString messageText = message->text;
                if (!message->caption.isEmpty())
                {
                    if (messageText.isEmpty())
                        messageText = message->caption;
                    else
                        messageText = message->caption + '\n' + messageText;
                }
                botMsh = botMsh.arg(messageText)
                               .arg(trigger->activationReasonMessage)
                               .arg(trigger->name);

                botMsh.replace("+", "&#43;");
                botMsh.replace("<", "&#60;");
                botMsh.replace(">", "&#62;");

                if (!trigger->description.isEmpty())
                    botMsh += QString(" (%1)").arg(trigger->description);

                tbot::HttpParams params2;
                params2["chat_id"] = chatId;
                params2["text"] = botMsh;
                params2["parse_mode"] = "HTML";
                emit sendTgCommand("sendMessage", params2, 1*1000 /*1 сек*/);

                if (TriggerTimeLimit* trg = dynamic_cast<TriggerTimeLimit*>(trigger))
                    if (!trg->messageInfo.isEmpty())
                    {
                        QTime timeBegin = !trg->activationTime.begin.isNull()
                                          ? trg->activationTime.begin
                                          : trg->activationTime.hint;

                        QTime timeEnd   = !trg->activationTime.end.isNull()
                                          ? trg->activationTime.end
                                          : trg->activationTime.hint;

                        QString message = trg->messageInfo;
                        message.replace("{begin}", timeBegin.toString("HH:mm"))
                               .replace("{end}",   timeEnd.toString("HH:mm"));

                        tbot::HttpParams params;
                        params["chat_id"] = chatId;
                        params["text"] = message;
                        params["parse_mode"] = "HTML";
                        emit sendTgCommand("sendMessage", params, 1.5*1000 /*1.5 сек*/,
                                           1, 3*60 /*3 мин*/);
                    }

                if (trigger->immediatelyBan)
                {
                    tbot::HttpParams params;
                    params["chat_id"] = chatId;
                    params["user_id"] = userId;
                    params["until_date"] = qint64(std::time(nullptr));
                    params["revoke_messages"] = true;
                    sendTgCommand("banChatMember", params, 3*1000 /*3 сек*/);
                }
                else if (trigger->reportSpam)
                {
                    // Отправляем отчет о спаме
                    emit reportSpam(chatId, message->from);
                }
                break;
            }
        }

        { //Block for QMutexLocker
            QMutexLocker locker {&_threadLock}; (void) locker;
            for (const QString& key : _mediaGroups.keys())
            {
                const MediaGroup& mg = _mediaGroups[key];
                if (mg.timer.elapsed() > 1*60*60*1000 /*1 час*/)
                {
                    log_debug_m << "Remove media group: " << key;
                    _mediaGroups.remove(key);
                }
            }
        }

    } // while (true)

    log_info_m << "Stopped";
}

} // namespace tbot
