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

Processing::Processing()
{}

bool Processing::init(qint64 botUserId, const QString& commandPrefix)
{
    _botUserId = botUserId;
    _commandPrefix = commandPrefix;
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

            if (_updates.isEmpty())
                continue;

            msgData = _updates.takeFirst();

            for (const TemporaryKey& key : _temporaryNewUsers.keys())
            {
                if (_temporaryNewUsers[key].elapsed() > 20*1000 /*20 сек*/)
                    _temporaryNewUsers.remove(key);
            }
        }
        if (msgData.empty() || msgData->data.isEmpty())
            continue;

        Update update;
        bool isBioMessage = false;

        // Обработка сообщения с BIO
        if (msgData->bio.userId > 0)
        {
            isBioMessage = true;
            update.update_id = msgData->bio.updateId;

            // Конструирование BIO сообщения
            UserBio userBio;
            if (!userBio.fromJson(msgData->data))
                continue;

            User::Ptr user {new User};
            user->id = msgData->bio.userId;
            user->first_name = userBio.first_name;
            user->last_name  = userBio.last_name;
            user->username   = userBio.username;

            Chat::Ptr chat {new Chat};
            chat->id = msgData->bio.chatId;
            
            Message::Ptr message {new Message};
            
            message->message_id = msgData->bio.messageId;
            message->text = userBio.bio;
            message->from = user;
            message->chat = chat;

            update.message = message;

            //log_verbose_m << "Emulation BIO message: " << update.toJson();
        }
        // Обработка обычного сообщения
        else
        {
            if (!update.fromJson(msgData->data))
                continue;
        }

        // Событие обновления прав для бота
        if (update.my_chat_member)
        {
            if (update.my_chat_member->chat)
            {
                // Обновляем информацию по администраторам для группы
                emit updateChatAdminInfo(update.my_chat_member->chat->id);
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
                    // Обновляем информацию по администраторам для группы
                    emit updateChatAdminInfo(update.chat_member->chat->id);
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

        if (botCommand(update))
            continue;

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

        log_verbose_m << log_format(R"("update_id":%?. Chat: %?. Clear text%?: %?)",
                                    update.update_id, chat->name(),
                                    (isBioMessage ? " [BIO]" : ""), clearText);

        if (isBioMessage && !chat->checkBio)
            continue;

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

        // Признак сообщения с информацией о новых пользователях
        bool isNewUsersMessage = message->new_chat_members.count();

        QList<User::Ptr> users;
        if (isNewUsersMessage)
            users = message->new_chat_members;
        else
            users.append(message->from);

        if (isNewUsersMessage || (isBioMessage && msgData->isNewUser))
            isNewUser = true;

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
            if (message->forward_from)
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
                    mg.isBad = true;
                    for (qint64 msgId : mg.messageIds)
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

bool Processing::botCommand(const Update& update)
{
    Message::Ptr message = (update.message)
                           ? update.message
                           : update.edited_message;
    if (message.empty())
    {
        // После выхода из функции будет обрабатываться следующее сообщение
        return true;
    }

    if (message->from.empty())
        return false;

    qint64 chatId = message->chat->id;
    qint64 userId = message->from->id;
    qint32 messageId = message->message_id;

    GroupChat::List chats = tbot::groupChats();
    GroupChat* chat = chats.findItem(&chatId);

    if (chat == nullptr)
        return false;

    auto sendMessage = [this, chatId](const QString& msg)
    {
        auto params = tgfunction("sendMessage");
        params->api["chat_id"] = chatId;
        params->api["text"] = msg;
        params->api["parse_mode"] = "HTML";
        params->delay = 1.5*1000 /*1.5 сек*/;
        emit sendTgCommand(params);
    };

    QString botMsg;
    for (const MessageEntity& entity : message->entities)
    {
        if (entity.type != "bot_command")
            continue;

        QString prefix = message->text.mid(entity.offset, entity.length);
        prefix.remove(QRegularExpression("@.*$"));

        if (prefix != _commandPrefix)
            continue;

        // Удаляем сообщение с командой
        {
            auto params = tgfunction("deleteMessage");
            params->api["chat_id"] = chatId;
            params->api["message_id"] = messageId;
            params->delay = 200 /*0.2 сек*/;
            emit sendTgCommand(params);
        }

        if (!chat->adminIds().contains(userId))
        {
            // Тайминг сообщения 1.5 сек
            sendMessage(u8"Для управления ботом нужны права администратора");

            // Штрафуем пользователя, который пытается управлять ботом (тайминг 3 сек)
            emit reportSpam(chatId, message->from);
            return true;
        }

        QString actionLine = message->text.mid(entity.offset + entity.length);

        const QRegularExpression::PatternOptions patternOpt =
                {QRegularExpression::DotMatchesEverythingOption
                |QRegularExpression::UseUnicodePropertiesOption};

        // Замена непечатных пробельных символов на обычные пробелы
        static QRegularExpression reSpaces {"[\\s\\.]", patternOpt};
        actionLine.replace(reSpaces, QChar(' '));

        QStringList lines = actionLine.split(QChar(' '), QString::SkipEmptyParts);

        auto printCommandsInfo = [&]()
        {
            botMsg =
                u8"Список доступных команд:"
                u8"\r\n%1 help [h]&#185; - "
                u8"Справка по списку команд;"
                u8"\r\n"

                u8"\r\n%1 version [v]&#185; - "
                u8"Версия бота;"
                u8"\r\n"

                u8"\r\n%1 timelimit [tl]&#185; (start|stop|status) - "
                u8"Активация/деактивация триггеров с типом timelimit;"
                u8"\r\n"

                u8"\r\n%1 spamreset [sr]&#185; ID - "
                u8"Аннулировать спам-штрафы для пользователя с идентификатором ID;"
                u8"\r\n"

//                u8"\r\n%1 globalblack [gb]&#185; ID - "
//                u8"Добавить пользователя с идентификатором ID в глобальный черный "
//                u8"список. Пользователь будет заблокирован во всех группах, где "
//                u8"установлен бот;"
//                u8"\r\n"

                u8"\r\n []&#185; - Сокращенное обозначение команды";

            sendMessage(botMsg.arg(_commandPrefix));
        };

        if (lines.isEmpty())
        {
            botMsg = u8"Список доступных команд можно посмотреть "
                     u8"при помощи команды: %1 help";
            sendMessage(botMsg.arg(_commandPrefix));
            return true;
        }

        QString command = lines.takeFirst();
        QStringList actions = lines;

        if ((command == "help") || (command == "h"))
        {
            // Описание команды для Телеграм:
            // telebot - help Справка по списку команд

            printCommandsInfo();
            return true;
        }
        else if ((command == "version") || (command == "v"))
        {
            botMsg = u8"Версия бота: %1 (gitrev:&#160;%2)"
                     u8"\r\nРепозиторий проекта: https://github.com/hkarel/Telebot";
            sendMessage(botMsg.arg(VERSION_PROJECT).arg(GIT_REVISION));
            return true;
        }
        else if ((command == "timelimit") || (command == "tl"))
        {

            QString action;
            if (actions.count())
                action = actions[0];

            if (action != "start"
                && action != "stop"
                && action != "status")
            {
                botMsg = (action.isEmpty())
                    ? u8"Для команды timelimit требуется указать действие."
                    : u8"Команда timelimit не используется с указанным действием: %1.";

                botMsg += u8"\r\nДопустимые действия: start/stop/status"
                          u8"\r\nПример: %2 timelimit status";
                botMsg = botMsg.arg(_commandPrefix).arg(action);

                sendMessage(botMsg);
                return true;
            }

            if (action == "start")
            {
                timelimitInactiveChatsRemove(chatId);
                emit updateBotCommands();

                sendMessage(u8"Триггер timelimit активирован");
            }
            else if (action == "stop")
            {
                timelimitInactiveChatsAdd(chatId);
                emit updateBotCommands();

                sendMessage(u8"Триггер timelimit деактивирован");
            }
            else if (action == "status")
            {
                for (tbot::Trigger* trigger : chat->triggers)
                {
                    TriggerTimeLimit* trg = dynamic_cast<TriggerTimeLimit*>(trigger);
                    if (trg == nullptr)
                        continue;

                    botMsg = u8"Триггер: " + trigger->name;
                    if (!trg->description.isEmpty())
                        botMsg += QString(" (%1)").arg(trg->description);

                    if (timelimitInactiveChats().contains(chatId))
                        botMsg += u8"\r\nСостояние: не активен";
                    else
                        botMsg += u8"\r\nСостояние: активен";

                    botMsg += u8"\r\nUTC часовой пояс: " + QString::number(trg->utc);
                    botMsg += u8"\r\nРасписание:";

                    bool first = true;
                    for (const TriggerTimeLimit::Day& day : trg->week)
                    {
                        QList<int> dayset = day.daysOfWeek.toList();
                        qSort(dayset.begin(), dayset.end());

                        botMsg += u8"\r\n    Дни:";
                        for (int ds : dayset)
                            botMsg += " " + QString::number(ds);

                        if (first)
                        {
                            botMsg += u8" (1 - понедельник)";
                            first = false;
                        }

                        botMsg += u8"\r\n    Время:";
                        for (const TriggerTimeLimit::TimeRange& time : day.times)
                        {
                            botMsg += u8"\r\n        Начало: ";
                            botMsg += !time.begin.isNull()
                                      ? time.begin.toString("HH:mm")
                                      : QString("--:--");

                            botMsg += u8". Окончание: ";
                            botMsg += !time.end.isNull()
                                      ? time.end.toString("HH:mm")
                                      : QString("--:--");

                            if (time.hint.isValid())
                            {
                                botMsg += u8". Подсказка: ";
                                botMsg += time.hint.toString("HH:mm");
                            }
                        }
                    }
                    botMsg += "\r\n\r\n";
                    sendMessage(botMsg);
                }
            }
            return true;
        }
        else if ((command == "spamreset") || (command == "sr"))
        {
            QString action = u8"пусто";
            if (actions.count())
                action = actions[0];

            bool ok;
            qint64 spamUserId = action.toLongLong(&ok);
            if (!ok)
            {
                log_error_m << log_format(
                    "Bot command 'spamreset'. Failed convert '%?' to user id", action);

                botMsg = u8"Команда spamreset. Ошибка преобразования "
                         u8"значения '%1' в идентификатор пользователя";
                sendMessage(botMsg.arg(action));
            }
            else
            {
                emit resetSpam(chatId, spamUserId);
            }
            return true;
        }
        else
        {
            log_error_m << log_format(
                "Unknown bot command: %? %?", _commandPrefix, command);

            botMsg = u8"Неизвестная команда: %1 %2";
            sendMessage(botMsg.arg(_commandPrefix).arg(command));
            return true;
        }
    }
    return false;
}

} // namespace tbot
