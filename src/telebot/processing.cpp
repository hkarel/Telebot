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
QList<MessageData::Ptr> Processing::_updates;
QMap<Processing::TemporaryKey, steady_timer> Processing::_temporaryNewUsers;
QMap<QString, Processing::MediaGroup> Processing::_mediaGroups;
Processing::VerifyAdmin::List Processing::_verifyAdmins;

Processing::Processing()
{}

bool Processing::init(qint64 botUserId)
{
    _botUserId = botUserId;
    _configChanged = true;
    return true;
}

void Processing::addUpdate(const MessageData::Ptr& msgData)
{
    QMutexLocker locker {&_threadLock}; (void) locker;

    //log_debug_m << "Webhook event data: " << data;

    _updates.append(msgData);
    _threadCond.wakeOne();
}

void Processing::addVerifyAdmin(qint64 chatId, qint64 userId, qint32 messageId)
{
    QMutexLocker locker {&_threadLock}; (void) locker;

    if (lst::FindResult fr = _verifyAdmins.findRef(qMakePair(chatId, userId)))
    {
        VerifyAdmin* va = _verifyAdmins.item(fr.index());
        va->messageIds.insert(messageId);
    }
    else
    {
        VerifyAdmin* va = _verifyAdmins.add();
        va->chatId = chatId;
        va->userId = userId;
        va->messageId = messageId;
        _verifyAdmins.sort();
    }
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
        MessageData::Ptr msgData;

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

            // Очистка списка VerifyAdmins
            for (int i = 0; i < _verifyAdmins.count(); ++i)
            {
                VerifyAdmin* va = _verifyAdmins.item(i);
                if (va->timer.elapsed() > 1*60*1000 /*1 мин*/)
                {
                    log_debug_m << log_format("Remove verify admin %?/%?/%?",
                                              va->chatId, va->userId, va->messageId);
                    _verifyAdmins.remove(i--);
                }
            }

            if (_updates.isEmpty())
                continue;

            msgData = _updates.takeFirst();

            for (const TemporaryKey& key : _temporaryNewUsers.keys())
            {
                if (_temporaryNewUsers[key].elapsed() > 20*1000 /*20 сек*/)
                    _temporaryNewUsers.remove(key);
            }
        }
        if (msgData.empty())
            continue;

        Update& update = msgData->update;
        bool isBioMessage = (msgData->bio.userId > 0);

        // Событие обновления прав для бота
        if (update.my_chat_member)
        {
            if (update.my_chat_member->chat)
            {
                // Обновляем информацию о группе и её администраторах
                emit reloadGroup(update.my_chat_member->chat->id, false);
            }
            else
                log_error_m << log_format(
                    R"("update_id":%?. Field my_chat_member->chat is empty)",
                    update.update_id);
        }

        // Признак нового пользователя (только что вошел в группу)
        bool isNewUser = false;

        // Событие обновления прав для админа/пользователя или вступления нового
        // пользователя в группу
        if (update.chat_member)
        {
            bool updateAdmins = false;
            if (update.chat_member->old_chat_member
                && update.chat_member->new_chat_member)
            {
                QString oldStatus = update.chat_member->old_chat_member->status;
                QString newStatus = update.chat_member->new_chat_member->status;

                if ((oldStatus == "member") && (newStatus == "administrator"))
                    updateAdmins = true;

                if ((oldStatus == "administrator") && (newStatus == "member"))
                    updateAdmins = true;

                // Новый пользователь вступил в группу
                if ((oldStatus == "left") && (newStatus == "member"))
                {
                    isNewUser = true;

                    // Всех обманываем и конструируем сообщение
                    Message::Ptr message {new Message};

                    message->message_id = -1;
                    message->from = update.chat_member->from;
                    message->chat = update.chat_member->chat;

                    update.message = message;
                }
            }
            if (updateAdmins)
            {
                if (update.chat_member->chat)
                {
                    // Обновляем информацию о группе и её администраторах
                    emit reloadGroup(update.chat_member->chat->id, false);
                }
                else
                    log_error_m << log_format(
                        R"("update_id":%?. Field chat_member->chat is empty)",
                        update.update_id);
            }
        }

        Message::Ptr message = update.message;

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

        qint64 chatId = message->chat->id;
        qint32 messageId = message->message_id;

        // Удаляем служебные сообщения Телеграм от имени бота
        if (message->from && (_botUserId == message->from->id /*userId*/))
        {
            auto params = tgfunction("deleteMessage");
            params->api["chat_id"] = chatId;
            params->api["message_id"] = messageId;
            params->delay = 200 /*0.2 сек*/;
            emit sendTgCommand(params);
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
                auto params = tgfunction("sendMessage");
                params->api["chat_id"] = chatId;
                params->api["text"] = _spamMessage;
                params->messageDel = -1;
                emit sendTgCommand(params);
            }
            continue;
        }

        GroupChat* chat = chats.item(fr.index());
        QSet<qint64> adminIds = chat->adminIds();
        ChatMemberAdministrator::Ptr botInfo = chat->botInfo();

        if (botInfo.empty())
            log_error_m << "Information about bot permissions is not available";

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

        log_verbose_m << log_format(R"("update_id":%?. Chat: %?. Clear text%?: %?)",
                                    update.update_id, chat->name(),
                                    (isBioMessage ? " [BIO]" : ""), clearText);

        if (isBioMessage && !chat->checkBio)
            continue;

        if (!message->media_group_id.isEmpty())
        {
            QMutexLocker locker {&_threadLock}; (void) locker;
            MediaGroup& mg = _mediaGroups[message->media_group_id];
            if (mg.isBad)
            {
                auto params = tgfunction("deleteMessage");
                params->api["chat_id"] = chatId;
                params->api["message_id"] = messageId;
                params->delay = 200 /*0.2 сек*/;
                emit sendTgCommand(params);
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
                mg.messageIds[messageId] = clearText.trimmed().isEmpty();
            }
        }

        tbot::Trigger::Text triggerText;

        //--- Trigger::TextType::Content ---
        triggerText[tbot::Trigger::TextType::Content] = clearText.trimmed();

        //--- Trigger::TextType::FrwdUserId ---
        if (message->forward_from)
            triggerText[tbot::Trigger::TextType::FrwdUserId] = message->forward_from->id;

        //--- Trigger::TextType::FileMime ---
        QString filemimeText;
        if (message->audio)
        {
            filemimeText += QString(" %1 %2 %3 %4")
                                   .arg(message->audio->performer)
                                   .arg(message->audio->title)
                                   .arg(message->audio->file_name)
                                   .arg(message->audio->mime_type);
            filemimeText = filemimeText.trimmed();
        }
        if (message->document)
        {
            filemimeText += QString(" %1 %2")
                                   .arg(message->document->file_name)
                                   .arg(message->document->mime_type);
            filemimeText = filemimeText.trimmed();
        }
        if (message->video)
        {
            filemimeText += QString(" %1 %2")
                                   .arg(message->video->file_name)
                                   .arg(message->video->mime_type);
            filemimeText = filemimeText.trimmed();
        }
        triggerText[tbot::Trigger::TextType::FileMime] = filemimeText;

        //--- Trigger::TextType::UrlLinks ---
        QString urllinksText;
        auto readEntities = [&urllinksText]
                            (const QString& text, const MessageEntity& entity)
        {
            if (entity.type == "url")
                urllinksText += text.mid(entity.offset, entity.length) + QChar(' ');

            else if (entity.type == "text_link")
                urllinksText += entity.url + QChar(' ');
        };
        for (const MessageEntity& entity : message->caption_entities)
            readEntities(message->caption, entity);

        for (const MessageEntity& entity : message->entities)
            readEntities(message->text, entity);

        triggerText[tbot::Trigger::TextType::UrlLinks] = urllinksText.trimmed();
        //---

        auto verifyAdmin = [&]() -> bool
        {
            auto deleteForwardMessage = [&]()
            {
                auto params = tgfunction("deleteMessage");
                params->api["chat_id"] = chatId;
                params->api["message_id"] = messageId;
                params->delay = 200 /*0.2 сек*/;
                emit sendTgCommand(params);
            };

            { //Block for QMutexLocker
                QMutexLocker locker {&_threadLock}; (void) locker;

                qint64 userId = message->from->id;
                lst::FindResult fr = _verifyAdmins.findRef(qMakePair(chatId, userId));
                if (fr.failed())
                    return false;

                VerifyAdmin* va = _verifyAdmins.item(fr.index());
                if (va->messageIds.contains(messageId))
                {
                    deleteForwardMessage();
                    return true;
                }
                if (va->messageId != messageId)
                    return false;
            }

            if (!message->forward_origin)
            {
                log_error_m << log_format(
                    R"("update_id":%?. Field message->forward_origin is empty)",
                    update.update_id);
                return false;
            }

            deleteForwardMessage();

            QString botMsg;
            if (message->forward_origin->type == "user")
            {
                if (!message->forward_origin->sender_user)
                {
                    log_error_m << log_format(
                        R"("update_id":%?. Field message->forward_origin->sender_user is empty)",
                        update.update_id);
                    return false;
                }

                auto stringUserInfo = [&](const User::Ptr& user) -> QString
                {
                    // Формируем строку с описанием пользователя
                    //   Смотри решение с Markdown разметкой тут:
                    //   https://core.telegram.org/bots/api#markdown-style
                    QString str = QString("[%2 %3](tg://user?id=%4)")
                                         .arg(user->first_name)
                                         .arg(user->last_name)
                                         .arg(user->id);

                    if (!user->username.isEmpty())
                    {
                        QString s = QString(" (@%1/%2)").arg(user->username).arg(user->id);
                        str += s.replace("_", "\\_");
                    }
                    else
                        str += QString(" (id: %1)").arg(user->id);

                    return str;
                };

                if (adminIds.contains(message->forward_origin->sender_user->id))
                {
                    botMsg =
                        u8"Верификация администратора."
                        u8"\r\nПользователь %1"
                        u8"\r\nявляется администратором группы";
                }
                else
                {
                    botMsg =
                        u8"Верификация администратора."
                        u8"\r\n❗️❗️❗️ *ВНИМАНИЕ* ❗️❗️❗️"
                        u8"\r\nПользователь %1"
                        u8"\r\n*НЕ* является администратором группы,"
                        u8" возможно это мошенник";
                }
                botMsg = botMsg.arg(stringUserInfo(message->forward_origin->sender_user));
            }
            else if (message->forward_origin->type == "hidden_user")
            {
                botMsg =
                    u8"Верификация администратора."
                    u8"\r\n❗️❗️❗️ *ВНИМАНИЕ* ❗️❗️❗️"
                    u8"\r\nПользователь _%1_ скрыл свои данные,"
                    u8" проверить его принадлежность к администратором группы *НЕЛЬЗЯ*,"
                    u8" возможно это мошенник";

                botMsg = botMsg.arg(message->forward_origin->sender_user_name);
            }
            else
            {
                log_error_m << log_format(
                    R"("update_id":%?. Unknown field value for message->forward_origin->type)",
                    update.update_id);
                return false;
            }

            if (!botMsg.isEmpty())
            {
                auto params = tgfunction("sendMessage");
                params->api["chat_id"] = chatId;
                params->api["text"] = botMsg;
                params->api["parse_mode"] = "Markdown";
                params->delay = 300 /*0.3 сек*/;
                params->messageDel = 15 /*15 сек*/;
                emit sendTgCommand(params);
            }
            return true;
        };

        // Признак сообщения с информацией о новых пользователях
        bool isNewUsersMessage = message->new_chat_members.count();

        QList<User::Ptr> users;
        if (isNewUsersMessage)
        {
            users = message->new_chat_members;
        }
        else if (isNewUser
                 && update.chat_member
                 && update.chat_member->new_chat_member
                 && update.chat_member->new_chat_member->user)
        {
            users.append(update.chat_member->new_chat_member->user);
        }
        else
        {
            users.append(message->from);
        }

        if (isNewUsersMessage || (isBioMessage && msgData->isNewUser))
            isNewUser = true;

        if (!isNewUsersMessage)
        {
            // Обрабатываем команду бота verifyadmin
            if (verifyAdmin())
                continue;
        }

        for (int i = 0; i < users.count(); ++i)
        {
            User::Ptr user = users[i];
            if (user.empty())
            {
                log_error_m << log_format(R"("update_id":%?. User struct is empty)",
                                          update.update_id);
                continue;
            }

            // Исключаем двойное срабатывание триггеров для новых пользователей
            if (isNewUser && !isBioMessage)
            {
                QMutexLocker locker {&_threadLock}; (void) locker;

                TemporaryKey temporaryKey {chatId, user->id};
                if (_temporaryNewUsers.contains(temporaryKey))
                {
                    log_verbose_m << log_format(
                        R"("update_id":%?. Chat: %?. Redetect new user %?/%?/@%?/%?. User skipped)",
                        update.update_id, chat->name(),
                        user->first_name, user->last_name, user->username, user->id);
                    continue;
                }
                _temporaryNewUsers[temporaryKey] = steady_timer();
            }

            // Проверка пользователя на принадлежность к списку администраторов
            if (chat->skipAdmins && adminIds.contains(user->id))
            {
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Triggers skipped, user %?/%?/@%?/%? is admin)",
                    update.update_id, chat->name(),
                    user->first_name, user->last_name, user->username, user->id);
                continue;
            }

            // Проверка пользователя на принадлежность к белому списку группы
            if (chat->whiteUsers.contains(user->id))
            {
                log_verbose_m << log_format(
                    R"("update_id":%?. Chat: %?. Triggers skipped, user %?/%?/@%?/%? in group whitelist)",
                    update.update_id, chat->name(),
                    user->first_name, user->last_name, user->username, user->id);
                continue;
            }

            // Проверка на Anti-Raid режим
            if (chat->antiRaid.active && isNewUser && !isBioMessage)
            {
                emit antiRaidUser(chatId, message->from);
                if (chat->antiRaidTurnOn)
                    continue;
            }

            // Признак премиум аккаунта у пользователя
            bool isPremium = false;

            //--- Trigger::TextType::UserId ---
            triggerText[tbot::Trigger::TextType::UserId] = user->id;

            //--- Trigger::TextType::UserName ---
            QString usernameText;
            if (user /*message->from*/)
            {
                usernameText += QString(" %1 %2 %3")
                                       .arg(user->first_name)
                                       .arg(user->last_name)
                                       .arg(user->username);
                usernameText = usernameText.trimmed();
                isPremium = user->is_premium;
            }
            if (message->forward_from
                && !adminIds.contains(message->forward_from->id)
                && !chat->whiteUsers.contains(message->forward_from->id))
            {
                usernameText += QString(" %1 %2 %3")
                                       .arg(message->forward_from->first_name)
                                       .arg(message->forward_from->last_name)
                                       .arg(message->forward_from->username);
                usernameText = usernameText.trimmed();
            }
            if (message->sender_chat)
            {
                usernameText += QString(" %1 %2 %3 %4")
                                       .arg(message->sender_chat->title)
                                       .arg(message->sender_chat->first_name)
                                       .arg(message->sender_chat->last_name)
                                       .arg(message->sender_chat->username);
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
            triggerText[tbot::Trigger::TextType::UserName] = usernameText;
            //---

            auto stringUserInfo = [&](const User::Ptr& user) -> QString
            {
                // Формируем строку с описанием пользователя
                //   Смотри решение с Markdown разметкой тут:
                //   https://core.telegram.org/bots/api#markdown-style
                QString str = QString("%1 => [%2 %3](tg://user?id=%4)")
                                     .arg(user->id)
                                     .arg(user->first_name)
                                     .arg(user->last_name)
                                     .arg(user->id);

                if (!user->username.isEmpty())
                {
                    QString s = QString(" (@%1)").arg(user->username);
                    str += s.replace("_", "\\_");
                }
                return str;
            };

            auto deleteMessage = [&](tbot::Trigger* trigger)
            {
                if (!message->media_group_id.isEmpty())
                {
                    QMutexLocker locker {&_threadLock}; (void) locker;
                    MediaGroup& mg = _mediaGroups[message->media_group_id];

                    if (dynamic_cast<TriggerEmptyText*>(trigger))
                    {
                        // Если на момент срабатывания триггера TriggerEmptyText
                        // не все сообщения в медиагруппе пустые - выходим из функции
                        for (qint64 msgId : mg.messageIds.keys())
                            if (!mg.messageIds[msgId])
                                return;
                    }

                    mg.isBad = true;
                    for (qint64 msgId : mg.messageIds.keys())
                    {
                        auto params = tgfunction("deleteMessage");
                        params->api["chat_id"] = mg.chatId;
                        params->api["message_id"] = msgId;
                        params->delay = 200 /*0.2 сек*/;
                        emit sendTgCommand(params);
                    }
                    mg.messageIds.clear();
                }
                else
                {
                    auto params = tgfunction("deleteMessage");
                    params->api["chat_id"] = chatId;
                    params->api["message_id"] = messageId;
                    params->delay = 200 /*0.2 сек*/;
                    emit sendTgCommand(params);
                }

                // Отправляем в Телеграм сообщение с описанием причины удаления сообщения
                QString botMsg =
                    u8"Бот удалил сообщение"
                    u8"\r\n---"
                    u8"\r\n%1"
                    u8"\r\n---"
                    u8"%2"
                    u8"\r\nПричина удаления сообщения"
                    u8"\r\n%3%4;"
                    u8"\r\nтриггер: %5";

                QString messageText;
                if (!isBioMessage)
                {
                    messageText = message->text;
                    if (!message->caption.isEmpty())
                    {
                        if (messageText.isEmpty())
                            messageText = message->caption;
                        else
                            messageText = message->caption + '\n' + messageText;
                    }
                }
                else
                    messageText = msgData->bio.messageOrigin;

                if (messageText.length() > 1000)
                {
                    messageText.resize(1000);
                    messageText += u8"…";
                }

                QString bioText;
                if (isBioMessage)
                    bioText = u8"\r\nBIO: " + message->text;

                botMsg = botMsg.arg(messageText)
                               .arg(bioText)
                               .arg(trigger->activationReasonMessage)
                               .arg(isBioMessage ? u8" [в BIO]" : u8"")
                               .arg(trigger->name);

                botMsg.replace("+", "&#43;");
                botMsg.replace("<", "&#60;");
                botMsg.replace(">", "&#62;");

                if (!trigger->description.isEmpty())
                    botMsg += QString(" (%1)").arg(trigger->description);

                auto params2 = tgfunction("sendMessage");
                params2->api["chat_id"] = chatId;
                params2->api["text"] = botMsg;
                params2->api["parse_mode"] = "HTML";
                params2->delay = 1*1000 /*1 сек*/;
                emit sendTgCommand(params2);

                // Формируем сообщение с идентификатором пользователя
                botMsg = stringUserInfo(user);

                // Отправляем сообщение с идентификатором пользователя
                auto params3 = tgfunction("sendMessage");
                params3->api["chat_id"] = chatId;
                params3->api["text"] = botMsg;
                params3->api["parse_mode"] = "Markdown";
                params3->delay = 1.3*1000 /*1.3 сек*/;
                emit sendTgCommand(params3);

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

                        auto params = tgfunction("sendMessage");
                        params->api["chat_id"] = chatId;
                        params->api["text"] = message;
                        params->api["parse_mode"] = "HTML";
                        params->delay = 1.5*1000 /*1.5 сек*/;
                        params->messageDel = 3*60 /*3 мин*/;
                        emit sendTgCommand(params);
                    }
            };

            auto restrictUser = [&](tbot::Trigger* trigger)
            {
                if (isNewUser)
                {
                    if (trigger->newUserBan || trigger->immediatelyBan)
                    {
                        log_verbose_m << log_format(
                            R"("update_id":%?. Chat: %?. Ban new user %?/%?/@%?/%?)",
                            update.update_id, chat->name(),
                            user->first_name, user->last_name, user->username, user->id);

                        auto params = tgfunction("banChatMember");
                        params->api["chat_id"] = chatId;
                        params->api["user_id"] = user->id;
                        params->api["until_date"] = qint64(std::time(nullptr));
                        params->api["revoke_messages"] = true;
                        params->delay = 200 /*0.2 сек*/;
                        sendTgCommand(params);

                        // Отправляем в Телеграм сообщение с описанием причины
                        // блокировки пользователя
                        QString botMsg =
                            u8"Бот заблокировал пользователя"
                            u8"\r\n%1"
                            u8"\r\n---"
                            u8"\r\nПричина блокировки"
                            u8"\r\n%2%3;"
                            u8"\r\nтриггер: %4";

                        QString reasonMessage = trigger->activationReasonMessage;
                        reasonMessage.replace("_", "\\_");

                        QString triggerName = trigger->name;
                        triggerName.replace("_", "\\_");

                        botMsg = botMsg.arg(stringUserInfo(user))
                                       .arg(reasonMessage)
                                       .arg(isBioMessage ? u8" \\[в BIO]" : u8"")
                                       .arg(triggerName);

                        // botMsg.replace("+", "&#43;");
                        // botMsg.replace("<", "&#60;");
                        // botMsg.replace(">", "&#62;");

                        if (!trigger->description.isEmpty())
                        {
                            QString s = QString(" (%1)").arg(trigger->description);
                            botMsg += s.replace("[", "\\[");
                        }

                        auto params2 = tgfunction("sendMessage");
                        params2->api["chat_id"] = chatId;
                        params2->api["text"] = botMsg;
                        params2->api["parse_mode"] = "Markdown";
                        params2->delay = 1*1000 /*1 сек*/;
                        emit sendTgCommand(params2);
                    }
                    else
                    {
                        log_verbose_m << log_format(
                            R"("update_id":%?. Chat: %?. Skipped new user %?/%?/@%?/%?)",
                            update.update_id, chat->name(),
                            user->first_name, user->last_name, user->username, user->id);
                    }
                }
                else
                {
                    if (trigger->immediatelyBan
                        || (isPremium && chat->premiumBan && trigger->premiumBan))
                    {
                        auto params = tgfunction("banChatMember");
                        params->api["chat_id"] = chatId;
                        params->api["user_id"] = user->id;
                        params->api["until_date"] = qint64(std::time(nullptr));
                        params->api["revoke_messages"] = true;
                        params->delay = 3*1000 /*3 сек*/;
                        sendTgCommand(params);
                    }
                    else if (trigger->reportSpam)
                    {
                        // Отправляем отчет о спаме
                        emit reportSpam(chatId, user);
                    }
                }
            };

            bool messageDeleted = false;
            bool userRestricted = false;

            for (tbot::Trigger* trigger : chat->triggers)
            {
                if (!trigger->active)
                {
                    log_verbose_m << log_format(
                        R"("update_id":%?. Chat: %?. Trigger '%?' skipped, it not active)",
                        update.update_id, chat->name(), trigger->name);
                    continue;
                }

                // Для анализа новых пользователей используем только TriggerRegexp
                if (isNewUser && !dynamic_cast<TriggerRegexp*>(trigger))
                    continue;

                if (isBioMessage && !trigger->checkBio)
                    continue;

                if (!isBioMessage && trigger->onlyBio)
                    continue;

                // Проверка пользователя на принадлежность к списку администраторов
                if (trigger->skipAdmins && adminIds.contains(user->id))
                {
                    log_verbose_m << log_format(
                        R"("update_id":%?. Chat: %?. Trigger '%?' skipped, user %?/%?/@%?/%? is admin)",
                        update.update_id, chat->name(), trigger->name,
                        user->first_name, user->last_name, user->username, user->id);
                    continue;
                }

                // Проверка пользователя на принадлежность к белому списку триггера
                if (trigger->whiteUsers.contains(user->id))
                {
                    log_verbose_m << log_format(
                        R"("update_id":%?. Chat: %?. Trigger '%?' skipped, user %?/%?/@%?/%? in trigger whitelist)",
                        update.update_id, chat->name(), trigger->name,
                        user->first_name, user->last_name, user->username, user->id);
                    continue;
                }

                bool triggerActive = trigger->isActive(update, chat, triggerText);

                if (trigger->inverse)
                    triggerActive = !triggerActive;

                if (!triggerActive)
                    continue;

                // Если триггер активирован, то запускаем механизмы удаления сообщения
                // и бана пользователя

                if (botInfo && botInfo->can_delete_messages)
                {
                    if (!isNewUser)
                    {
                        log_verbose_m << log_format(
                            R"("update_id":%?. Chat: %?. Delete message (message_id: %?), user %?/%?/@%?/%?)",
                            update.update_id, chat->name(), messageId,
                            user->first_name, user->last_name, user->username, user->id);

                        deleteMessage(trigger);
                        messageDeleted = true;
                    }
                }
                else
                {
                    log_error_m << "The bot does not have rights to delete message";
                }

                if (botInfo && botInfo->can_restrict_members)
                {
                    restrictUser(trigger);
                    userRestricted = true;
                }
                else
                {
                    log_error_m << "The bot does not have rights to ban user";
                }

                break;
            }

            // Отправляем запрос на получение BIO
            if (chat->checkBio
                && !isBioMessage && !messageDeleted && !userRestricted)
            {
                QString messageText = message->text;
                if (!message->caption.isEmpty())
                {
                    if (messageText.isEmpty())
                        messageText = message->caption;
                    else
                        messageText = message->caption + '\n' + messageText;
                }
                auto params = tgfunction("getChat");
                params->api["chat_id"] = user->id;
                params->delay = 200 /*0.2 сек*/;
                params->bio.userId = user->id;
                params->bio.chatId = chatId;
                params->bio.updateId = update.update_id;
                params->bio.messageId = messageId;
                params->bio.messageOrigin = messageText.trimmed();
                params->isNewUser = isNewUser;
                sendTgCommand(params);
            }

            // Ограничение пользователя на два и более часа, если он в течении
            // одной минуты подключается к нескольким группам
            if (isNewUser && !isBioMessage)
                emit restrictNewUser(chat->id, user->id, chat->newUserMute);

            // Проверка на Anti-Raid режим
            if (chat->antiRaid.active && chat->antiRaidTurnOn
                && !isBioMessage && !messageDeleted && !userRestricted)
            {
                emit antiRaidMessage(chatId, user->id, messageId);
            }

        } // for (int i = 0; i < users.count(); ++i)

        //--- Мониторинг скрытого тегирования пользователей ---
        QList<QPair<qint32 /*offset*/, qint32 /*unicode*/>> textMentions;
        for (const MessageEntity& entity : message->entities)
        {
            if (entity.type != "text_mention")
                continue;

            qint64 usrId = (entity.user) ? entity.user->id : 0;
            if (adminIds.contains(usrId) && (entity.length == 1))
                if (entity.offset < message->text.length())
                {
                    QChar ch = message->text[entity.offset];
                    textMentions.append({entity.offset, qint32(ch.unicode())});
                }
        }
        if (!textMentions.isEmpty())
        {
            log_debug_m << log_format(
                R"("update_id":%?. Non print mention symbols {offset, unicode}: %?)",
                update.update_id, textMentions);
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
