#include "telebot_appl.h"

#include "trigger.h"
#include "functions.h"
#include "group_chat.h"

#include "shared/spin_locker.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"
#include "shared/config/appl_conf.h"
#include "shared/config/logger_conf.h"
#include "shared/qt/logger_operators.h"
#include "shared/qt/version_number.h"

#include "pproto/commands/pool.h"
#include "pproto/logger_operators.h"

#include <QHostInfo>

#define log_error_m   alog::logger().error   (alog_line_location, "Application")
#define log_warn_m    alog::logger().warn    (alog_line_location, "Application")
#define log_info_m    alog::logger().info    (alog_line_location, "Application")
#define log_verbose_m alog::logger().verbose (alog_line_location, "Application")
#define log_debug_m   alog::logger().debug   (alog_line_location, "Application")
#define log_debug2_m  alog::logger().debug2  (alog_line_location, "Application")

#define KILL_TIMER(TIMER) {if (TIMER != -1) {killTimer(TIMER); TIMER = -1;}}

namespace tbot {
extern std::atomic_int globalConfigParceErrors;
}

QUuidEx Application::_applId;
volatile bool Application::_stop = false;
std::atomic_int Application::_exitCode = {0};

static quint64 httpReplyNumber = {0};

static QByteArray unicodeDecode(const QByteArray& data)
{
    QString source = QString::fromUtf8(data);
    QString dest; dest.reserve(source.length());

    auto getUint8 = [](uchar h, uchar l) -> uchar
    {
        uint8_t ret = 0;

        if      (h - '0' < 10) ret = h - '0';
        else if (h - 'A' < 6 ) ret = h - 'A' + 0x0A;
        else if (h - 'a' < 6 ) ret = h - 'a' + 0x0A;

        ret = ret << 4;

        if      (l - '0' < 10) ret |= l - '0';
        else if (l - 'A' < 6 ) ret |= l - 'A' + 0x0A;
        else if (l - 'a' < 6 ) ret |= l - 'a' + 0x0A;

        return  ret;
    };

    for (auto&& it = source.cbegin(); it != source.cend(); ++it)
    {
        if (*it == QChar('\\')
            && std::distance(it, source.cend()) > 5)
        {
            if (*(it + 1) == QChar('u'))
            {
                uchar c1 = getUint8((it + 2)->cell(), (it + 3)->cell());
                uchar c2 = getUint8((it + 4)->cell(), (it + 5)->cell());

                quint16 v = (c1 << 8) | c2;
                dest.append(QChar(v));

                it += 5;
                continue;
            }
        }
        dest.append(*it);
    }
    return dest.toUtf8();
}

void SslServer::incomingConnection(qintptr socketDescriptor)
{
    QSslSocket* socket = new QSslSocket(this);
    socket->setSocketDescriptor(socketDescriptor);
    addPendingConnection(socket);
}

Application::Application(int& argc, char** argv)
    : QCoreApplication(argc, argv)
{
    _stopTimerId         = startTimer(1000);
    _slaveTimerId        = startTimer(10*1000 /*10 сек*/);
    _antiraidTimerId     = startTimer( 2*1000 /* 2 сек*/);
    _timelimitTimerId    = startTimer(15*1000 /*15 сек*/);
    _userJoinTimerId     = startTimer(30*1000 /*30 сек*/);
    _configStateTimerId  = startTimer(10*1000 /*10 сек*/);
    _updateAdminsTimerId = startTimer(4*60*60*1000 /*4 часа*/);

    chk_connect_a(&config::observerBase(), &config::ObserverBase::changed,
                  this, &Application::reloadConfig)

    chk_connect_a(&config::observer(), &config::Observer::changedItem,
                  this, &Application::reloadGroups)

    #define FUNC_REGISTRATION(COMMAND) \
        _funcInvoker.registration(command:: COMMAND, &Application::command_##COMMAND, this);

    FUNC_REGISTRATION(SlaveAuth)
    FUNC_REGISTRATION(ConfSync)
    FUNC_REGISTRATION(TimelimitSync)
    FUNC_REGISTRATION(UserTriggerSync)
    FUNC_REGISTRATION(DeleteDelaySync)
    FUNC_REGISTRATION(UserJoinTimeSync)

    #undef FUNC_REGISTRATION

    qRegisterMetaType<ReplyData>("ReplyData");
    qRegisterMetaType<tbot::User::Ptr>("tbot::User::Ptr");
    qRegisterMetaType<tbot::TgParams::Ptr>("tbot::TgParams::Ptr");
}

bool Application::init()
{
    _botId.clear();
    if (!config::base().getValue("bot.id", _botId))
        return false;

    _webhookServer = SslServer::Ptr::create();
    _networkAccManager = QNetworkAccessManagerPtr::create();

    chk_connect_a(_webhookServer.get(), &QTcpServer::newConnection,
                  this, &Application::webhook_newConnection)

    _localServer = false;
    config::base().getValue("local_server.active", _localServer);

    _localServerAddr = "127.0.0.1";
    config::base().getValue("local_server.address", _localServerAddr);

    _localServerPort = 0;
    config::base().getValue("local_server.port", _localServerPort);

    quint16 port = 0;
    if (_localServer)
    {
        config::base().getValue("local_server.webhook_port", port);
        log_verbose_m << "Start webhook local-server"
                      << ". Address: " << QHostAddress::AnyIPv4
                      << ". Port: " << port;
    }
    else
    {
        config::base().getValue("webhook.port", port);
        log_verbose_m << "Start webhook cloud-server"
                      << ". Address: " << QHostAddress::AnyIPv4
                      << ". Port: " << port;
    }

    if (!_webhookServer->listen(QHostAddress::AnyIPv4, port))
    {
        log_error_m << "Failed start TCP-server"
                    << ". Error: " << _webhookServer->errorString();
        return false;
    }

    if (!_localServer)
    {
        QString certPath;
        config::base().getValue("webhook.certificate", certPath);

        QFile file;
        file.setFileName(certPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            log_error_m << "Failed open cert file: " << certPath;
            return false;
        }
        QByteArray certData = file.readAll();
        file.close();

        _sslCert = QSslCertificate(certData);
        if (_sslCert.isNull())
        {
            log_error_m << "Failed ssl cert data. File: " << certPath;
            return false;
        }
        if (_sslCert.subjectInfo(QSslCertificate::CommonName).isEmpty())
        {
            log_error_m << "Failed ssl cert. CN field is empty. File: " << certPath;
            return false;
        }

        QString keyPath;
        config::base().getValue("webhook.private_key", keyPath);

        file.setFileName(keyPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            log_error_m << "Failed open key file: " << keyPath;
            return false;
        }
        QByteArray keyData = file.readAll();
        file.close();

        _sslKey = QSslKey(keyData, QSsl::KeyAlgorithm::Rsa);
        if (_sslKey.isNull())
        {
            log_error_m << "Failed ssl key data. File: " << keyPath;
            return false;
        }
    }

    loadReportSpam();
    reportSpam(0, {}); // Выводим в лог текущий спам-список

    loadBotCommands();
    loadAntiRaidCache();

    startRequest();

    _botStartTime = QDateTime::currentDateTimeUtc();
    return true;
}

void Application::deinit()
{
    if (_listenerInit)
        tcp::listener().close();

    if (_slaveSocket)
        _slaveSocket->disconnect();

    for (tbot::Processing* p : _procList)
        p->stop();

    for (auto&& it = _webhookMap.cbegin(); it != _webhookMap.cend(); ++it)
    {
        const WebhookData& wd = it.value();
        log_debug_m << log_format(
            "QObject::disconnect(). Webhook socket descriptor: %?",
            wd.socketDescr);

        QObject::disconnect(wd.socket, nullptr, this, nullptr);
    }

    for (auto&& it = _httpReplyMap.cbegin(); it != _httpReplyMap.cend(); ++it)
    {
        const ReplyData& rd = it.value();
        log_debug_m << log_format(
            "QObject::disconnect(). Http reply id: %?", rd.replyNumer);

        QObject::disconnect(rd.reply, nullptr, this, nullptr);
    }

    _procList.clear();
    _webhookServer->close();

    _webhookServer.reset();
    _networkAccManager.reset();

    saveReportSpam();
    saveAntiRaidCache();

    if (tbot::userJoinTimes().changed())
    {
        qint64 timemark = QDateTime::currentDateTime().toMSecsSinceEpoch();
        saveBotCommands(user_join_time, timemark);
    }
}

void Application::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == _stopTimerId)
    {
        if (_stop)
        {
            KILL_TIMER(_stopTimerId)
            KILL_TIMER(_slaveTimerId)
            KILL_TIMER(_antiraidTimerId)
            KILL_TIMER(_timelimitTimerId)
            KILL_TIMER(_userJoinTimerId)
            KILL_TIMER(_configStateTimerId)
            KILL_TIMER(_updateAdminsTimerId)

            exit(_exitCode);
            return;
        }

        // Закрываем сокеты не прошедшие авторизацию за отведенное время
        for (int i = 0; i < _waitAuthSockets.count(); ++i)
            if (_waitAuthSockets[i].second.elapsed() > 3.1*1000 /*3.1 сек*/)
            {
                tcp::Socket::Ptr socket =
                    tcp::listener().socketByDescriptor(_waitAuthSockets[i].first)
                                   .dynamic_cast_to<tcp::Socket::Ptr>();
                if (socket)
                {
                    data::CloseConnection closeConnection;
                    closeConnection.description = "Authorization timeout expired";

                    Message::Ptr m = createJsonMessage(closeConnection);
                    m->appendDestinationSocket(socket->socketDescriptor());
                    tcp::listener().send(m);

                    log_error_m << "Authorization timeout expired"
                                << ". Remote host: " << socket->peerPoint()
                                << ". Connection will be closed";

                    _waitAuthSockets[i].second.reset();
                    _waitCloseSockets.append(_waitAuthSockets[i]);
                }
                _waitAuthSockets.remove(i--);
            }

        for (int i = 0; i < _waitCloseSockets.count(); ++i)
            if (_waitCloseSockets[i].second.elapsed() > 2.5*1000 /*2.5 сек*/)
            {
                tcp::Socket::Ptr socket =
                    tcp::listener().socketByDescriptor(_waitCloseSockets[i].first)
                                   .dynamic_cast_to<tcp::Socket::Ptr>();
                if (socket)
                {
                    log_error_m << "Force close of connection"
                                << ". Remote host: " << socket->peerPoint();
                    socket->disconnect();
                }
                _waitCloseSockets.remove(i--);
            }

        //--- Anti-Raid ---
        tbot::GroupChat::List chats = tbot::groupChats();
        for (AntiRaid* antiRaid : _antiRaidCache)
        {
            qint64 chatId = antiRaid->chatId;
            tbot::GroupChat* chat = chats.findItem(&chatId);

            if (chat == nullptr)
            {
                // Не удаляем  элемент  AntiRaid из _antiRaidCache так как
                // при старте приложения список _antiRaidCache заполняется
                // раньше списка групп, поэтому он будет полностью вычищен
                // в этой точке.
                // _antiRaidCache.remove(i--);
                continue;
            }
            if (!chat->antiRaid.active)
                continue;

            if (chat->antiRaidTurnOn)
            {
                QDateTime currentTime = QDateTime::currentDateTimeUtc();
                if ((currentTime > antiRaid->deactiveTime) || !chat->antiRaid.active)
                {
                    chat->antiRaidTurnOn = false;
                    antiRaid->deactiveTime = QDateTime();

                    log_verbose_m << log_format("Chat: %?. Anti-Raid mode turn off",
                                                chat->name());

                    // Отправляем в Телеграм сообщение о деактивации Anti-Raid режима
                    QString botMsg =
                        u8"Бот деактивировал <i>Anti-Raid</i> режим."
                        u8"\r\nНовые пользователи могут присоединяться к группе";

                    auto params = tbot::tgfunction("sendMessage");
                    params->api["chat_id"] = chatId;
                    params->api["text"] = botMsg;
                    params->api["parse_mode"] = "HTML";
                    params->messageDel = -1;
                    sendTgCommand(params);
                }
            }
            else
            {
                // Удаляем из кеша пользователей тех, у кого временное
                // значение вышло за границу timeFrame
                int timeFrame = chat->antiRaid.timeFrame * 1000;
                for (int i = 0; i < antiRaid->usersTmp.count(); ++i)
                    if (antiRaid->usersTmp[i].antiraid_timer.elapsed() > timeFrame)
                        antiRaid->usersTmp.remove(i--);

                int usersLimit = chat->antiRaid.usersLimit;
                if (antiRaid->usersTmp.count() >= usersLimit)
                {
                    // Активируем Anti-Raid режим
                    chat->antiRaidTurnOn = true;

                    log_verbose_m << log_format("Chat: %?. Anti-Raid mode turn on",
                                                chat->name());

                    int duration = chat->antiRaid.duration * 60; // в секундах
                    QDateTime currentTime = QDateTime::currentDateTimeUtc();
                    antiRaid->deactiveTime = currentTime.addSecs(duration);

                    for (tbot::User* user : antiRaid->usersTmp)
                    {
                        log_verbose_m << log_format(
                            "Chat: %?. Anti-Raid mode is active, user %?/%?/@%?/%? added to ban list",
                            chat->name(), user->first_name, user->last_name, user->username, user->id);

                        user->add_ref();
                        antiRaid->usersBan.add(user);
                    }
                    antiRaid->usersBan.sort();
                    antiRaid->usersTmp.clear();

                    // Пауза в удалении участников на 3x2 => 6 секунд
                    antiRaid->sleepBanCount = 3;

                    // Пауза в удалении сообщений на 6 секунд
                    antiRaid->sleepMsgCount = 6;

                    // Отправляем в Телеграм сообщение об активации Anti-Raid режима
                    QString botMsg =
                        u8"❗️❗️❗️ <b>ВНИМАНИЕ</b> ❗️❗️❗️"
                        u8"\r\nЗафиксирована <i>Raid-атака</i> на группу."
                        u8"\r\nБот активировал <i>Anti-Raid</i> режим."
                        u8"\r\nАдминистраторам группы необходимо"
                        u8"\r\nв кратчайщие сроки запретить публикацию"
                        u8"\r\nвсех видов сообщений."
                        u8"\r\nПользователи присоединившиеся к группе"
                        u8"\r\nв <i>Anti-Raid</i> режиме будут заблокированы."
                        u8"\r\n---"
                        u8"\r\n%1";

                    QString anames;
                    for (const QString& s : chat->adminNames())
                        anames += "@" + s + " ";

                    auto params = tbot::tgfunction("sendMessage");
                    params->api["chat_id"] = chatId;
                    params->api["text"] = botMsg.arg(anames);
                    params->api["parse_mode"] = "HTML";
                    params->messageDel = -1;
                    sendTgCommand(params);
                }
            }

            if (antiRaid->skipMessageIds
                && antiRaid->skipMessageIdsTimer.elapsed() > 10*1000 /*10 сек*/)
            {
                antiRaid->skipMessageIds = false;
            }

            if (antiRaid->sleepMsgCount > 0)
                --antiRaid->sleepMsgCount;
            else
                antiRaid->sleepMsgCount = 0;

            if (antiRaid->messageIds.count()
                && !antiRaid->skipMessageIds
                && !antiRaid->sleepMsgCount)
            {
                qint32 messageId = antiRaid->messageIds[0];
                antiRaid->skipMessageIds = true;
                antiRaid->skipMessageIdsTimer.reset();

                auto params = tbot::tgfunction("deleteMessage");
                params->api["chat_id"] = chatId;
                params->api["message_id"] = messageId;
                params->isAntiRaid = true;
                sendTgCommand(params);

                log_verbose_m << log_format(
                    "Chat: %?. Anti-Raid mode active, remove message %?",
                    chat->name(), messageId);
            }
        }

        //--- NewUser ---
        for (int i = 0; i < _newUsers.count(); ++i)
        {
            NewUser* newUser = _newUsers.item(i);
            if (newUser->timer.elapsed() >= 60*1000 /*1 мин*/)
                _newUsers.remove(i--);
        }

        //--- AdjacentMessage ---
        auto adjacentDeleteMessage = [this](AdjacentMessage* adjacentMsg)
        {
            auto params = tbot::tgfunction("deleteMessage");
            params->api["chat_id"] = adjacentMsg->chatId;
            params->api["message_id"] = adjacentMsg->messageId;
            params->delay = 200 /*0.2 сек*/;
            sendTgCommand(params);

            if (!adjacentMsg->text.trimmed().isEmpty())
            {
                QString botMsg =
                        u8"Бот удалил сообщение"
                        u8"\r\n---"
                        u8"\r\n%1"
                        u8"\r\n---"
                        u8"\r\nПричина удаления: сопутствующее спаму сообщение";

                auto params2 = tbot::tgfunction("sendMessage");
                params2->api["chat_id"] = adjacentMsg->chatId;
                params2->api["text"] = botMsg.arg(adjacentMsg->text);
                params2->api["parse_mode"] = "HTML";
                params2->delay = 1*1000 /*1 сек*/;
                sendTgCommand(params2);
            }
        };

        // Удаляем сопутствующие спаму сообщения с истекшим сроком
        for (int i = 0; i < _adjacentMessages.count(); ++i)
            if (_adjacentMessages[i].timer.elapsed() >= 6*1000 /*6 сек*/)
                _adjacentMessages.remove(i--);

        // Удаляем предыдущие сопутствующие спаму сообщения
        for (int i = 1; i < _adjacentMessages.count(); ++i)
        {
            AdjacentMessage* adjacentMsg = _adjacentMessages.item(i);
            if (adjacentMsg->deleted)
            {
                AdjacentMessage* am = _adjacentMessages.item(i - 1);
                if (!am->deleted
                    && am->chatId == adjacentMsg->chatId
                    && am->userId == adjacentMsg->userId
                    && am->messageId == (adjacentMsg->messageId - 1))
                {
                    adjacentDeleteMessage(am);
                    _adjacentMessages.remove(i - 1);
                    --i;
                }
            }
        }

        // Удаляем последующие сопутствующие спаму сообщения
        for (int i = 0; i < _adjacentMessages.count() - 1; ++i)
        {
            AdjacentMessage* adjacentMsg = _adjacentMessages.item(i);
            if (adjacentMsg->deleted)
            {
                AdjacentMessage* am = _adjacentMessages.item(i + 1);
                if (!am->deleted
                    && am->chatId == adjacentMsg->chatId
                    && am->userId == adjacentMsg->userId
                    && am->messageId == (adjacentMsg->messageId + 1))
                {
                    if (!am->mediaGroupId.isEmpty())
                        adjacentMsg->mediaGroupForDelete = am->mediaGroupId;

                    adjacentDeleteMessage(am);
                    _adjacentMessages.remove(i + 1);
                }
            }
        }

        // Удаляем сопутствующие спаму медиагруппы сообщений
        for (int i = 0; i < _adjacentMessages.count(); ++i)
        {
            AdjacentMessage* adjacentMsg = _adjacentMessages.item(i);
            if (!adjacentMsg->mediaGroupForDelete.isEmpty())
                for (int j = i + 1; j < _adjacentMessages.count(); ++j)
                {
                    AdjacentMessage* am = _adjacentMessages.item(j);
                    if (!am->deleted
                        && adjacentMsg->mediaGroupForDelete == am->mediaGroupId)
                    {
                        adjacentDeleteMessage(am);
                        _adjacentMessages.remove(j--);
                    }
                }
        }

        //--- DeleteDelay ---
        bool deleteDelayActive = false;
        qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
        for (int i = 0; i < _deleteDelays.count(); ++i)
        {
            const data::DeleteDelay& dd = _deleteDelays[i];
            if (currentTime > dd.deleteTime)
            {
                auto params = tbot::tgfunction("deleteMessage");
                params->api["chat_id"] = dd.chatId;
                params->api["message_id"] = dd.messageId;
                sendTgCommand(params);

                _deleteDelays.removeAt(i--);
                deleteDelayActive = true;
            }
        }
        if (deleteDelayActive)
            updateBotCommands(delete_delay);
    }
    else if (event->timerId() == _antiraidTimerId)
    {
        for (AntiRaid* antiRaid : _antiRaidCache)
        {
            if (antiRaid->sleepBanCount > 0)
            {
                --antiRaid->sleepBanCount;
                continue;
            }

            if (antiRaid->messageIds.count())
                continue;

            if (antiRaid->skipUsersBan
                && antiRaid->skipUsersBanTimer.elapsed() > 10*1000 /*10 сек*/)
            {
                antiRaid->skipUsersBan = false;
            }
            if (antiRaid->skipUsersBan)
                continue;

            qint64 chatId = antiRaid->chatId;
            if (antiRaid->usersBan.count())
            {
                tbot::User* user = antiRaid->usersBan.item(0);
                antiRaid->skipUsersBan = true;
                antiRaid->skipUsersBanTimer.reset();

                auto params = tbot::tgfunction("banChatMember");
                params->api["chat_id"] = chatId;
                params->api["user_id"] = user->id;
                params->api["until_date"] = qint64(std::time(nullptr));
                params->api["revoke_messages"] = true;
                params->isAntiRaid = true;
                //params->delay = 30*1000; Для отладки
                sendTgCommand(params);
            }
        }

        // Резервное сохранение списка Anti-Raid пользователей
        if (_antiRaidSaveStep++ >= 10 /*интервал 10x2 => 20 сек*/)
        {
            _antiRaidSaveStep = 0;
            saveAntiRaidCache();
        }
    }
    else if (event->timerId() == _slaveTimerId)
    {
        reloadBotMode();
    }
    else if (event->timerId() == _timelimitTimerId)
    {
        if (_masterMode || (_slaveSocket && !_slaveSocket->isConnected()))
        {
            // Проверяем необходимость публикации сообщений для timelimit триггеров
            timelimitCheck();
        }
    }
    else if (event->timerId() == _userJoinTimerId)
    {
        if (tbot::userJoinTimes().changed())
        {
            updateBotCommands(user_join_time);
            tbot::userJoinTimes().resetChangeFlag();
            config::state().saveFile();
        }
    }
    else if (event->timerId() == _configStateTimerId)
    {
        if (config::state().changed())
            config::state().saveFile();
    }
    else if (event->timerId() == _updateAdminsTimerId)
    {
        if (_masterMode || (_slaveSocket && !_slaveSocket->isConnected()))
        {
            log_verbose_m << "Update groups config-file by timer";

            // Обновляем информацию о группах и списках администраторов
            tbot::GroupChat::List chats = tbot::groupChats();
            for (int i = 0; i < chats.count(); ++i)
            {
                tbot::GroupChat* chat = chats.item(i);
                auto params = tbot::tgfunction("getChat");
                params->api["chat_id"] = chat->id;
                params->delay = 2*60*1000 /*2 мин*/ * i;
                sendTgCommand(params);
            }
        }
    }
}

void Application::stop(int exitCode)
{
    _exitCode = exitCode;
    stop();
}

void Application::message(const Message::Ptr& message)
{
    // Не обрабатываем сообщения если приложение получило команду на остановку
    if (_stop)
        return;

    if (message->processed())
        return;

    if (message->command() != command::SlaveAuth)
    {
        // Не обрабатываем сообщения от сокетов не прошедших авторизацию
        for (int i = 0; i < _waitAuthSockets.count(); ++i)
            if (_waitAuthSockets[i].first == message->socketDescriptor())
                return;

        for (int i = 0; i < _waitCloseSockets.count(); ++i)
            if (_waitCloseSockets[i].first == message->socketDescriptor())
                return;
    }

    if (lst::FindResult fr = _funcInvoker.findCommand(message->command()))
    {
        if (command::pool().commandIsSinglproc(message->command()))
            message->markAsProcessed();
        _funcInvoker.call(message, fr);
    }
}

void Application::socketConnected(SocketDescriptor socketDescr)
{
    if (!_masterMode)
    {
        QString password;
        config::base().getValue("bot.slave.password", password);

        Message::Ptr m = createJsonMessage(command::SlaveAuth);
        m->setAccessId(password.toUtf8());

        _slaveSocket->send(m);
        return;
    }

    // Master mode
    _waitAuthSockets.append({socketDescr, steady_timer()});
}

void Application::socketDisconnected(SocketDescriptor socketDescr)
{
    for (int i = 0; i < _waitAuthSockets.count(); ++i)
        if (_waitAuthSockets[i].first == socketDescr)
            _waitAuthSockets.remove(i--);

    for (int i = 0; i < _waitCloseSockets.count(); ++i)
        if (_waitCloseSockets[i].first == socketDescr)
            _waitCloseSockets.remove(i--);

    if (!_masterMode)
        _masterStartTime = QDateTime();
}

void Application::command_SlaveAuth(const Message::Ptr& message)
{
    if (!_masterMode && (message->type() == Message::Type::Answer))
    {
        _masterStartTime = QDateTime();
        if (message->execStatus() == Message::ExecStatus::Success)
        {
            log_info_m << "Success authorization on master-bot"
                       << ". Remote host: " << message->sourcePoint();

            quint64 tag0 = message->tag(0);
            if (tag0 != 0)
                _masterStartTime = QDateTime::fromSecsSinceEpoch(qint64(tag0), Qt::UTC);

            QTimer::singleShot(5*1000 /*5 сек*/, [this]()
            {
                if (Application::isStopped())
                    return;

                // Отправляем запрос на получение конфигурационного файла
                Message::Ptr m = createJsonMessage(command::ConfSync);
                if (_slaveSocket)
                    _slaveSocket->send(m);
            });
        }
        else
        {
            QString msg = errorDescription(message);
            log_error_m << msg;
        }
        return;
    }

    // Master mode
    Message::Ptr answer = message->cloneForAnswer();

    QString password;
    config::base().getValue("bot.master.password", password);

    if (password == QString::fromUtf8(message->accessId()))
    {
        for (int i = 0; i < _waitAuthSockets.count(); ++i)
            if (_waitAuthSockets[i].first == message->socketDescriptor())
                _waitAuthSockets.remove(i--);

        // Отправляем подтверждение об успешной операции: пустое сообщение.
        // В tag(0) отправляется время старта master-бота
        answer->setTag(quint64(_botStartTime.toSecsSinceEpoch()), 0);
        tcp::listener().send(answer);

        log_info_m << "Success authorization slave-bot"
                   << ". Remote host: " << message->sourcePoint();

        // Отправлем команду синхронизации timelimit
        qint64 timemark = 0;
        config::state().getValue("timelimit_inactive.timemark", timemark);

        data::TimelimitSync timelimitSync;
        timelimitSync.timemark = timemark;
        timelimitSync.chats = tbot::timelimitInactiveChats();

        Message::Ptr m = createJsonMessage(timelimitSync);
        m->appendDestinationSocket(answer->socketDescriptor());
        tcp::listener().send(m);

        // Отправлем команду синхронизации user_trigger
        timemark = 0;
        config::state().getValue("user_trigger.timemark", timemark);

        data::UserTriggerSync userTriggerSync;
        userTriggerSync.timemark = timemark;
        userTriggerSync.triggers.assign(_userTriggers);

        m = createJsonMessage(userTriggerSync);
        m->appendDestinationSocket(answer->socketDescriptor());
        tcp::listener().send(m);

        // Отправлем команду синхронизации delete_delay
        timemark = 0;
        config::state().getValue("delete_delay.timemark", timemark);

        data::DeleteDelaySync deleteDelaySync;
        deleteDelaySync.timemark = timemark;
        deleteDelaySync.items = _deleteDelays;

        m = createJsonMessage(deleteDelaySync);
        m->appendDestinationSocket(answer->socketDescriptor());
        tcp::listener().send(m);

        // Отправлем команду синхронизации user_join_time
        timemark = 0;
        config::state().getValue("user_join_time.timemark", timemark);

        data::UserJoinTimeSync userJoinTimeSync;
        userJoinTimeSync.timemark = timemark;
        userJoinTimeSync.items = tbot::userJoinTimes().list();

        m = createJsonMessage(userJoinTimeSync);
        m->appendDestinationSocket(answer->socketDescriptor());
        tcp::listener().send(m);

        return;
    }

    // Ошибка авторизации
    data::MessageFailed failed;
    failed.description = "Failed authorization";

    writeToJsonMessage(failed, answer);
    tcp::listener().send(answer);

    data::CloseConnection closeConnection;
    closeConnection.description = failed.description;

    Message::Ptr m = createJsonMessage(closeConnection);
    m->appendDestinationSocket(message->socketDescriptor());
    tcp::listener().send(m);

    for (int i = 0; i < _waitAuthSockets.count(); ++i)
        if (_waitAuthSockets[i].first == message->socketDescriptor())
        {
            _waitAuthSockets[i].second.reset();
            _waitCloseSockets.append(_waitAuthSockets[i]);
            _waitAuthSockets.remove(i--);
        }

    log_error_m << "Failed authorization"
                << ". Remote host: " << message->sourcePoint()
                << ". Connection will be closed";
}

void Application::command_ConfSync(const Message::Ptr& message)
{
    // Путь к конфиг-файлу (master/slave) с телеграм-группами
    QString configFileM = QString(CONFIG_DIR) + "/telebot.groups";
    QString configFileS = QString(VAROPT_DIR) + "/state/telebot.groups";

    if (_masterMode)
    {
        Message::Ptr answer = message->cloneForAnswer();

        if (!QFile::exists(configFileM))
        {
            writeToJsonMessage(error::config_not_exists, answer);
            tcp::listener().send(answer);
            return;
        }

        QFile file {configFileM};
        if (!file.open(QIODevice::ReadOnly))
        {
            writeToJsonMessage(error::config_not_read, answer);
            tcp::listener().send(answer);
            return;
        }
        QByteArray conf = file.readAll();
        file.close();

        data::ConfSync confSync;
        confSync.config = QString::fromUtf8(conf);

        writeToJsonMessage(confSync, answer);
        tcp::listener().send(answer);
        return;
    }

    // Slave mode
    data::ConfSync confSync;
    readFromMessage(message, confSync);

    if (confSync.config.isEmpty())
    {
        log_error_m << "Groups config file is empty";
        return;
    }

    // Проверяем структуру config-файла
    YamlConfig yconfig; (void) yconfig;
    if (!yconfig.readString(confSync.config.toStdString()))
    {
        log_error_m << "Groups config file structure is corrupted";
        return;
    }

    config::observer().stop();

    QFile file {configFileS};
    if (!file.open(QIODevice::WriteOnly))
    {
        log_error_m << "Failed open to save config file: " << configFileS;
        config::observer().start();
        return;
    }
    file.write(confSync.config.toUtf8());
    file.close();

    log_verbose_m << "Groups config file updated from master-bot: " << configFileS;

    reloadGroups(configFileS);
    config::observer().start();
}

void Application::command_TimelimitSync(const Message::Ptr& message)
{
    data::TimelimitSync timelimitSync;
    readFromMessage(message, timelimitSync);

    qint64 timemark = 0;
    config::state().getValue("timelimit_inactive.timemark", timemark);

    // Для master и slave режимов перезаписываем устаревшие данные
    if (timelimitSync.timemark > timemark)
    {
        tbot::setTimelimitInactiveChats(timelimitSync.chats);
        saveBotCommands(timelimit_inactive, timelimitSync.timemark);

        log_verbose_m << "Updated 'timelimit' settings for groups";
    }
    else
    {
        // Если у slave-бота значение timemark больше чем у master-a,
        // то выполняем обратную синхронизацию
        if (_slaveSocket && (message->type() != Message::Type::Answer))
        {
            log_verbose_m << "Send 'timelimit' settings to master-bot";

            Message::Ptr answer = message->cloneForAnswer();

            data::TimelimitSync timelimitSync;
            timelimitSync.timemark = timemark;
            timelimitSync.chats = tbot::timelimitInactiveChats();

            writeToJsonMessage(timelimitSync, answer);
            _slaveSocket->send(answer);
        }
    }
}

void Application::command_UserTriggerSync(const Message::Ptr& message)
{
    data::UserTriggerSync userTriggerSync;
    readFromMessage(message, userTriggerSync);

    qint64 timemark = 0;
    config::state().getValue("user_trigger.timemark", timemark);

    // Для master и slave режимов перезаписываем устаревшие данные
    if (userTriggerSync.timemark > timemark)
    {
        _userTriggers = std::move(userTriggerSync.triggers);
        for (data::UserTrigger* userTrgList : _userTriggers)
            userTrgList->items.sort();
        _userTriggers.sort();

        saveBotCommands(user_trigger, userTriggerSync.timemark);

        log_verbose_m << "Updated 'user_trigger' settings for groups";
    }
    else
    {
        // Если у slave-бота значение timemark больше чем у master-a,
        // то выполняем обратную синхронизацию
        if (_slaveSocket && (message->type() != Message::Type::Answer))
        {
            log_verbose_m << "Send 'user_trigger' settings to master-bot";

            Message::Ptr answer = message->cloneForAnswer();

            data::UserTriggerSync userTriggerSync;
            userTriggerSync.timemark = timemark;
            userTriggerSync.triggers.assign(_userTriggers);

            writeToJsonMessage(userTriggerSync, answer);
            _slaveSocket->send(answer);
        }
    }
}

void Application::command_DeleteDelaySync(const Message::Ptr& message)
{
    data::DeleteDelaySync deleteDelaySync;
    readFromMessage(message, deleteDelaySync);

    qint64 timemark = 0;
    config::state().getValue("delete_delay.timemark", timemark);

    // Для master и slave режимов перезаписываем устаревшие данные
    if (deleteDelaySync.timemark > timemark)
    {
        _deleteDelays = deleteDelaySync.items;
        saveBotCommands(delete_delay, deleteDelaySync.timemark);

        log_verbose_m << "Updated 'delete_delay' settings for groups";
    }
    else
    {
        // Если у slave-бота значение timemark больше чем у master-a,
        // то выполняем обратную синхронизацию
        if (_slaveSocket && (message->type() != Message::Type::Answer))
        {
            log_verbose_m << "Send 'delete_delay' settings to master-bot";

            Message::Ptr answer = message->cloneForAnswer();

            data::DeleteDelaySync deleteDelaySync;
            deleteDelaySync.timemark = timemark;
            deleteDelaySync.items = _deleteDelays;

            writeToJsonMessage(deleteDelaySync, answer);
            _slaveSocket->send(answer);
        }
    }
}

void Application::command_UserJoinTimeSync(const Message::Ptr& message)
{
    data::UserJoinTimeSync userJoinTimeSync;
    readFromMessage(message, userJoinTimeSync);

    qint64 timemark = 0;
    config::state().getValue("user_join_time.timemark", timemark);

    // Для master и slave режимов перезаписываем устаревшие данные
    if (userJoinTimeSync.timemark > timemark)
    {
        tbot::userJoinTimes().listSwap(userJoinTimeSync.items);
        tbot::userJoinTimes().resetChangeFlag();
        saveBotCommands(user_join_time, userJoinTimeSync.timemark);

        log_verbose_m << "Updated 'user_join_time' settings for groups";
    }
    else
    {
        // Если у slave-бота значение timemark больше чем у master-a,
        // то выполняем обратную синхронизацию
        if (_slaveSocket && (message->type() != Message::Type::Answer))
        {
            log_verbose_m << "Send 'user_join_time' settings to master-bot";

            Message::Ptr answer = message->cloneForAnswer();

            data::UserJoinTimeSync userJoinTimeSync;
            userJoinTimeSync.timemark = timemark;
            userJoinTimeSync.items = tbot::userJoinTimes().list();

            writeToJsonMessage(userJoinTimeSync, answer);
            _slaveSocket->send(answer);
        }
    }
}

void Application::webhook_newConnection()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(_webhookServer->nextPendingConnection());

    chk_connect_a(socket, &QSslSocket::readyRead,
                  this,   &Application::webhook_readyRead);

    chk_connect_a(socket, &QSslSocket::disconnected,
                  this,   &Application::webhook_disconnected);

    chk_connect_a(socket, qOverload<QAbstractSocket::SocketError>(&QSslSocket::error),
                  this,   &Application::webhook_socketError);

    chk_connect_a(socket, qOverload<const QList<QSslError>& >(&QSslSocket::sslErrors),
                  this,   &Application::webhook_sslSocketError);

    chk_connect_a(socket, &QSslSocket::encrypted,
                  this,   &Application::webhook_readyConnection);

    if (!_localServer)
    {
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        socket->setLocalCertificateChain(QList<QSslCertificate>{_sslCert});
        socket->setPrivateKey(_sslKey);
        socket->setProtocol(QSsl::TlsV1_3OrLater);
        socket->startServerEncryption();
    }

    quint64 socketId = reinterpret_cast<quint64>(socket);
    WebhookData& wd = _webhookMap[socketId];
    wd.socket = socket;
    wd.socketDescr = socket->socketDescriptor();

    log_debug_m << log_format(
        "Webhook connection from host: %?:%?. Socket descriptor: %?",
        socket->peerAddress(), socket->peerPort(), socket->socketDescriptor());
}

void Application::webhook_readyConnection()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());

    log_debug_m << log_format(
        "Webhook connection ready. IsEncrypted: %?. Socket descriptor: %?",
        socket->isEncrypted(), socket->socketDescriptor());
}

void Application::webhook_disconnected()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    quint64 socketId = reinterpret_cast<quint64>(socket);

    log_debug_m << log_format("Webhook disconnected. Socket descriptor: %?",
                              _webhookMap[socketId].socketDescr);

    _webhookMap.remove(socketId);
    socket->deleteLater();
}

void Application::webhook_readyRead()
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());
    QByteArray data = socket->readAll();

    log_debug_m << "Webhook TCP input: " << data;

    quint64 socketId = reinterpret_cast<quint64>(socket);
    WebhookData& wd = _webhookMap[socketId];

    if (wd.dataSize == -1)
    {
        // Разбираем заголовок
        int pos1 = data.indexOf("Content-Length:");
        if (pos1 == -1)
        {
            log_error_m << "HTTP atribyte 'Content-Length' not found";
            socket->close();
            return;
        }
        pos1 += 15;
        int pos2 = data.indexOf("\r\n", pos1);
        QByteArray len = data.mid(pos1, pos2 - pos1);

        bool ok;
        wd.dataSize = len.toInt(&ok);
        if (!ok)
        {
            log_error_m << "Failed get 'Content-Length' atribyte";
            socket->close();
            return;
        }

        if (data.indexOf("Connection: keep-alive") != -1)
            wd.keepAlive = true;

        int pos3 = data.indexOf("\r\n\r\n", pos2);
        pos3 += 4;
        data.remove(0, pos3);
        if (data.isEmpty())
            return;
    }

    wd.data.append(data);

    if (wd.data.size() == wd.dataSize)
    {
        wd.data = unicodeDecode(wd.data);
        int pos = wd.data.indexOf('\n');
        if (pos > 0)
            wd.data.remove(pos, 1);

        log_verbose_m << "Webhook TCP data: " << wd.data;

        if (!wd.data.isEmpty())
        {
            tbot::MessageData::Ptr msgData {tbot::MessageData::Ptr::create()};
            if (msgData->update.fromJson(wd.data))
            {
                bool chatInList = true;
                tbot::Message::Ptr message = (msgData->update.message)
                                             ? msgData->update.message
                                             : msgData->update.edited_message;
                if (message && message->chat)
                {
                    const qint64 chatId = message->chat->id;
                    tbot::GroupChat::List chats = tbot::groupChats();

                    lst::FindResult fr = chats.findRef(chatId);
                    if (fr.failed())
                    {
                        chatInList = false;
                        log_warn_m << log_format("Group chat %? not belong to list chats"
                                                 " in config. It skipped", chatId);

                        if (_spamIsActive && !_spamMessage.isEmpty())
                        {
                            auto params = tbot::tgfunction("sendMessage");
                            params->api["chat_id"] = chatId;
                            params->api["text"] = _spamMessage;
                            params->messageDel = -1;
                            sendTgCommand(params);
                        }
                    }
                }

                if (chatInList)
                {
                    if (!botCommand(msgData))
                        sendToProcessing(msgData);
                }
            }
        }

        wd.dataSize = -1;
        wd.data.clear();

        if (wd.keepAlive)
            socket->write("HTTP/1.1 200 OK\r\n\r\n");
        else
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");

        socket->flush();
        return;
    }

    if (wd.data.size() > wd.dataSize)
    {
        log_error_m << log_format(
            "Input data larger than 'Content-Length' => %? %?",
            wd.data.size(), wd.dataSize);
        socket->close();
    }
}

void Application::webhook_socketError(QAbstractSocket::SocketError error)
{
    QSslSocket* socket = qobject_cast<QSslSocket*>(sender());

    switch (error)
    {
        case QAbstractSocket::RemoteHostClosedError:
            log_verbose_m << "Webhook remote host closed";
            break;

        case QAbstractSocket::HostNotFoundError:
            log_error_m << "HostNotFoundError";
            break;

        case QAbstractSocket::ConnectionRefusedError:
            log_error_m << "ConnectionRefusedError";
            break;

        default:
            log_error_m << "Webhook error: " << socket->errorString()
                        << ". Socket descriptor: " << socket->socketDescriptor();
    }
}

void Application::webhook_sslSocketError(const QList<QSslError>& /*errors*/)
{
    log_error_m << "Webhook: sslSocketError";
}

void Application::http_readyRead()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    quint64 replyId = reinterpret_cast<quint64>(reply);
    ReplyData& rd = _httpReplyMap[replyId];

    rd.data.append(reply->readAll());
}

void Application::http_finished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    quint64 replyId = reinterpret_cast<quint64>(reply);
    ReplyData rd = _httpReplyMap[replyId];

    rd.data = unicodeDecode(rd.data);

    auto printToLog = [&rd]()
    {
        log_debug_m << log_format("Http answer (reply id: %?) func   : %? (attempt: %?)",
                                  rd.replyNumer, rd.params->funcName, rd.params->attempt);

        { //Block for alog::Line
            alog::Line logLine = log_debug_m << log_format(
                                  "Http answer (reply id: %?) params : ", rd.replyNumer);

            for (auto&& it = rd.params->api.cbegin(); it != rd.params->api.cend(); ++it)
                logLine << it.key() << ": " << it.value().toString() << "; ";
        }
        log_debug_m << log_format("Http answer (reply id: %?) return : %?",
                                  rd.replyNumer, rd.data);
    };

    if (rd.params->funcName == "getChat"
        || (rd.params->funcName == "getChatAdministrators"))
    {
        if (_printGetChat
            && rd.params->funcName == "getChat")
            printToLog();

        if (_printGetChatAdmins
            && rd.params->funcName == "getChatAdministrators")
            printToLog();
    }
    else
        printToLog();

    if (rd.params->isAntiRaid)
    {
        qint64 chatId = rd.params->api["chat_id"].toLongLong();
        if (AntiRaid* antiRaid = _antiRaidCache.findItem(&chatId))
        {
            if (rd.params->funcName == "banChatMember")
            {
                if (!rd.success)
                {
                    // Пауза в удалении Anti-Raid участников на 40x2 => 80 секунд
                    antiRaid->sleepBanCount = 40;
                    log_debug_m << log_format(
                        "Chat: %?. Set Anti-Raid sleep count %? for ban users",
                        chatId, antiRaid->sleepBanCount);
                }

                qint64 userId = rd.params->api["user_id"].toLongLong();
                if (lst::FindResult fr = antiRaid->usersBan.findRef(userId))
                {
                    // Удаляем участника из списка, чтобы избежать зацикливания
                    tbot::User* user = antiRaid->usersBan.item(fr.index());
                    log_verbose_m << log_format(
                        "Chat: %?. Remove from Anti-Raid list user %?/%?/@%?/%?",
                        chatId, user->first_name, user->last_name, user->username, user->id);

                    antiRaid->skipUsersBan = false;
                    antiRaid->usersBan.remove(fr.index());
                }
            }
            if (rd.params->funcName == "deleteMessage")
            {
                if (!rd.success)
                {
                    // Пауза в удалении Anti-Raid сообщений на 3 секунды
                    antiRaid->sleepMsgCount = 3;
                    log_debug_m << log_format(
                        "Chat: %?. Set Anti-Raid sleep count %? for remove messages",
                        chatId, antiRaid->sleepMsgCount);
                }

                // Удаляем сообщение из списка, чтобы избежать зацикливания
                qint32 messageId = rd.params->api["message_id"].toInt();
                log_verbose_m << log_format(
                    "Chat: %?. Remove from Anti-Raid list message %?",
                    chatId, messageId);

                antiRaid->skipMessageIds = false;
                antiRaid->messageIds.removeAll(messageId);
            }
        }
    }

    if (rd.success)
    {
        QMetaObject::invokeMethod(this, "httpResultHandler", Qt::QueuedConnection,
                                  Q_ARG(ReplyData, rd));
    }
    else
    {
        if (rd.params->funcName == "deleteMessage"
            || rd.params->funcName == "banChatMember"
            || rd.params->funcName == "restrictChatMember")
        {
            tbot::HttpResult httpResult;
            httpResult.fromJson(rd.data);

            // Ошибка 400: сообщение не найдено или недостаточно прав
            // для ограничения пользователя
            if (httpResult.error_code != 400)
            {
                auto params = tbot::TgParams::Ptr::create(*rd.params);
                params->attempt += 1;

                if (rd.params->attempt == 1)
                {
                    params->delay = 30*1000 /*30 сек*/;
                    sendTgCommand(params);
                }
                if (rd.params->attempt == 2)
                {
                    params->delay = 60*1000 /*60 сек*/;
                    sendTgCommand(params);
                }
            }
        }

        if (rd.params->funcName == "getChat"
            || rd.params->funcName == "getChatAdministrators")
        {
            auto params = tbot::TgParams::Ptr::create(*rd.params);
            params->attempt += 1;

            if (rd.params->attempt == 1)
            {
                params->delay = 20*1000 /*20 сек*/;
                sendTgCommand(params);
            }
            if (rd.params->attempt == 2)
            {
                params->delay = 30*1000 /*30 сек*/;
                sendTgCommand(params);
            }
            if (rd.params->attempt == 3)
            {
                params->delay = 60*1000 /*60 сек*/;
                sendTgCommand(params);
            }
            if (rd.params->attempt == 4)
            {
                params->delay = 2*60*1000 /*2 мин*/;
                sendTgCommand(params);
            }
            if (rd.params->attempt == 5)
            {
                params->delay = 2*60*1000 /*2 мин*/;
                sendTgCommand(params);
            }
            if (rd.params->attempt == 6)
            {
                params->delay = 3*60*1000 /*3 мин*/;
                sendTgCommand(params);
            }
        }
    }

    _httpReplyMap.remove(replyId);
    reply->deleteLater();
}

void Application::http_error(QNetworkReply::NetworkError /*error*/)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    quint64 replyId = reinterpret_cast<quint64>(reply);
    ReplyData& rd = _httpReplyMap[replyId];

    rd.success = false;
    log_error_m << log_format("Http error (reply id: %?). %?",
                              rd.replyNumer, reply->errorString());
}

void Application::http_encrypted()
{
    log_debug2_m << "http_encrypted()";
}

void Application::http_sslErrors(const QList<QSslError>&)
{
    log_error_m << "http_sslErrors()";
}

void Application::reloadConfig()
{
    QString masterModeStr;
    config::base().getValue("bot.work_mode", masterModeStr);

    _masterMode = !(masterModeStr == "slave");
    log_info_m << "Bot mode: " << (_masterMode ? "master" : "slave");

    // Путь к конфиг-файлу (master/slave) с телеграм-группами
    QString configFileM = QString(CONFIG_DIR) + "/telebot.groups";
    QString configFileS = QString(VAROPT_DIR) + "/state/telebot.groups";
    QString configFile;
    if (_masterMode)
    {
        config::observer().removeFile(configFileS);
        config::observer().addFile(configFileM);
        configFile = configFileM;
    }
    else
    {
        config::observer().removeFile(configFileM);
        config::observer().addFile(configFileS);
        configFile = configFileS;
    }

    _printTriggers = true;
    config::base().getValue("print_log.triggers", _printTriggers);

    _printGroupChats = true;
    config::base().getValue("print_log.group_chats", _printGroupChats);

    _printGetChat = true;
    config::base().getValue("print_log.get_chat_info", _printGetChat);

    _printGetChatAdmins = true;
    config::base().getValue("print_log.get_chat_administrators", _printGetChatAdmins);

    _spamIsActive = false;
    config::base().getValue("bot.spam_message.active", _spamIsActive);

    _spamMessage.clear();
    config::base().getValue("bot.spam_message.text", _spamMessage);

    reloadBotMode();
    reloadGroups(configFile);
}

void Application::reloadBotMode()
{
    if (_masterMode)
    {
        if (_slaveSocket)
        {
            _slaveSocket->disconnect();
            QObject::disconnect(_slaveSocket.get(), nullptr, this, nullptr);
            _slaveSocket.reset();
        }

        if (!_listenerInit)
        {
            chk_connect_q(&tcp::listener(), &tcp::Listener::message,
                          this, &Application::message)

            chk_connect_q(&tcp::listener(), &tcp::Listener::socketConnected,
                          this, &Application::socketConnected)

            chk_connect_q(&tcp::listener(), &tcp::Listener::socketDisconnected,
                          this, &Application::socketDisconnected)

            QHostAddress hostAddress = QHostAddress::AnyIPv4;
            config::readHostAddress("bot.master.address", hostAddress);

            int port = DEFAULT_PORT;
            config::base().getValue("bot.master.port", port);

#ifdef NDEBUG
            tcp::listener().setOnlyEncrypted(true);
#endif
            tcp::listener().setCompressionLevel(-1);
            _listenerInit = tcp::listener().init({hostAddress, port});
        }
    }
    else
    {
        if (_listenerInit)
        {
            tcp::listener().close();
            QObject::disconnect(&tcp::listener(), nullptr, this, nullptr);
            _listenerInit = false;
        }

        if (_slaveSocket.empty())
        {
            _slaveSocket = tcp::Socket::Ptr(new tcp::Socket);

            chk_connect_q(_slaveSocket.get(), &tcp::Socket::message,
                          this, &Application::message)

            chk_connect_q(_slaveSocket.get(), &tcp::Socket::connected,
                          this, &Application::socketConnected)

            chk_connect_q(_slaveSocket.get(), &tcp::Socket::disconnected,
                          this, &Application::socketDisconnected)
        }

        if (!_slaveSocket->isConnected())
        {
            QString strAddr;
            config::base().getValue("bot.slave.address", strAddr);

            int port = DEFAULT_PORT;
            config::base().getValue("bot.slave.port", port);

            QHostInfo hostInfo = QHostInfo::fromName(strAddr);
            if (hostInfo.error() != QHostInfo::NoError
                || hostInfo.addresses().isEmpty())
            {
                log_error_m << "Failed resolve network address " << strAddr;
                return;
            }
            QHostAddress addr = hostInfo.addresses()[0];

            if (!_slaveSocket->init({addr, port}))
                return;

#ifdef NDEBUG
            _slaveSocket->setEncryption(true);
            _slaveSocket->setEchoTimeout(5);
#endif
            _slaveSocket->setMessageFormat(SerializeFormat::Json);
            _slaveSocket->setCompressionLevel(-1);
            _slaveSocket->connect();
        }
    }
}

void Application::reloadGroup(qint64 chatId, bool botCommand)
{
    auto params = tbot::tgfunction("getChat");
    params->api["chat_id"] = chatId;
    params->isBotCommand = botCommand;
    sendTgCommand(params);
}

void Application::reloadGroups(const QString& configFile)
{
    QFileInfo configInfo {configFile};
    if (configInfo.fileName() != "telebot.groups")
        return;

    YamlConfig config;
    if (!config.readFile(configFile.toStdString()))
        return;

    tbot::globalConfigParceErrors = 0;

    { //Block for tbot::Trigger::List
        tbot::Trigger::List triggers;
        tbot::loadTriggers(triggers, config);

        if (_printTriggers)
            tbot::printTriggers(triggers);

        tbot::triggers(&triggers);
    }

    tbot::GroupChat::List oldChats;

    { //Block for tbot::Trigger::List
        tbot::GroupChat::List chats;
        tbot::loadGroupChats(chats, config);

        if (_printGroupChats)
            tbot::printGroupChats(chats);

        tbot::groupChats(&chats);
        oldChats.swap(chats);
    }

    if (tbot::globalConfigParceErrors == 0)
    {
        log_info_m << "---";
        log_info_m << "Success parse groups config-file";
        log_info_m << "---";
    }
    else
    {
        log_error_m << "---";
        log_error_m << "Failed parse groups config-file"
                    << ". Error count: " << int(tbot::globalConfigParceErrors);
        log_error_m << "---";
    }

    tbot::GroupChat::List newChats = tbot::groupChats();

    // Получение/обновление информации о группах и их администраторах
    if (newChats.count() > oldChats.count())
    {
        int index = 0;
        for (tbot::GroupChat* newChat : newChats)
        {
            if (oldChats.findRef(newChat->id))
                continue;

            auto params = tbot::tgfunction("getChat");
            params->api["chat_id"] = newChat->id;

            // Команды getChat отправляются с интервалом 100 миллисекунд, чтобы
            // уложиться в ограничение Телеграм на 30 запросов в секунду от бота.
            // Интервал 100 миллисекунд обеспечивает существенный временной запас,
            // это обусловлено тем, что после вызова getChat идет вызов команды
            // getChatAdministrators
            params->delay = 100 * index++;
            sendTgCommand(params);
        }
    }

    for (tbot::GroupChat* chat : newChats)
        if (AntiRaid* antiRaid = _antiRaidCache.findItem(&chat->id))
        {
            QDateTime currentTime = QDateTime::currentDateTimeUtc();
            if (antiRaid->deactiveTime > currentTime)
                chat->antiRaidTurnOn = true;
        }

    if (_masterMode)
    {
        QString configFileM = QString(CONFIG_DIR) + "/telebot.groups";

        QFile file {configFileM};
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray conf = file.readAll();
            file.close();

            data::ConfSync confSync;
            confSync.config = QString::fromUtf8(conf);

            // Отправляем событие с измененным конфигурационным файлом
            Message::Ptr m = createJsonMessage(confSync, {Message::Type::Event});
            tcp::listener().send(m);
        }
    }
}

void Application::startRequest()
{
    auto params = tbot::tgfunction("getMe");
    sendTgCommand(params);
}

void Application::timelimitCheck()
{
    using namespace tbot;

    for (int i = 0; i < _timelimitBegins.count(); ++i)
        if (_timelimitBegins[i].timer.elapsed() > 3*60*1000 /*3 мин*/)
            _timelimitBegins.remove(i--);

    for (int i = 0; i < _timelimitEnds.count(); ++i)
        if (_timelimitEnds[i].timer.elapsed() > 3*60*1000 /*3 мин*/)
            _timelimitEnds.remove(i--);

    GroupChat::List chats = tbot::groupChats();
    for (GroupChat* chat : chats)
    {
        if (timelimitInactiveChats().contains(chat->id))
            continue;

        for (Trigger* trigger : chat->triggers)
            if (TriggerTimeLimit* trg = dynamic_cast<TriggerTimeLimit*>(trigger))
            {
                QDateTime dtime = QDateTime::currentDateTimeUtc();
                dtime = dtime.addSecs(trg->utc * 60*60); // Учитываем сдвиг UTC
                int dayOfWeek = dtime.date().dayOfWeek();
                QTime curTime = dtime.time();

                TriggerTimeLimit::Times times;
                trg->timesRangeOfDay(dayOfWeek, times);
                for (const TriggerTimeLimit::TimeRange& time : times)
                {
                    if (!time.begin.isNull()
                        && !_timelimitBegins.findRef(qMakePair(chat->id, trg->name)))
                    {
                        QTime timeBeginL = time.begin.addSecs(+1  /*+1 сек*/);
                        QTime timeBeginR = time.begin.addSecs(+45 /*+45 сек*/);
                        if (timeInRange(timeBeginL, curTime, timeBeginR))
                        {
                            TimeLimit* tl = _timelimitBegins.add();
                            tl->chatId = chat->id;
                            tl->triggerName = trg->name;

                            _timelimitBegins.sort();

                            if (trg->active)
                            {
                                log_verbose_m << log_format(
                                    "Trigger timelimit '%?' started. Chat: %?",
                                    trg->name, chat->name());

                                if (!trg->messageBegin.isEmpty())
                                {
                                    QTime timeBegin = !time.begin.isNull() ? time.begin : time.hint;
                                    QTime timeEnd   = !time.end.isNull()   ? time.end   : time.hint;

                                    QString message = trg->messageBegin;
                                    message.replace("{begin}", timeBegin.toString("HH:mm"))
                                           .replace("{end}",   timeEnd.toString("HH:mm"));

                                    auto params = tbot::tgfunction("sendMessage");
                                    params->api["chat_id"] = chat->id;
                                    params->api["text"] = message;
                                    params->api["parse_mode"] = "HTML";
                                    params->messageDel = trg->hideMessageBegin ? 0 : 6*60*60 /*6 часов*/;
                                    sendTgCommand(params);
                                }
                            }
                            else
                            {
                                log_verbose_m << log_format(
                                    "Trigger timelimit '%?'  skipped, it not active. Chat: %?",
                                    trg->name, chat->name());
                            }
                        }
                    }

                    if (!time.end.isNull()
                        && !_timelimitEnds.findRef(qMakePair(chat->id, trg->name)))
                    {
                        QTime timeEndL = time.end.addSecs(+1  /*+1 сек*/);
                        QTime timeEndR = time.end.addSecs(+45 /*+45 сек*/);
                        if (timeInRange(timeEndL, curTime, timeEndR))
                        {
                            TimeLimit* tl = _timelimitEnds.add();
                            tl->chatId = chat->id;
                            tl->triggerName = trg->name;

                            _timelimitEnds.sort();

                            if (trg->active)
                            {
                                log_verbose_m << log_format(
                                    "Trigger timelimit '%?' stopped. Chat: %?",
                                    trg->name, chat->name());

                                if (!trg->messageEnd.isEmpty())
                                {
                                    QTime timeBegin = !time.begin.isNull() ? time.begin : time.hint;
                                    QTime timeEnd   = !time.end.isNull()   ? time.end   : time.hint;

                                    QString message = trg->messageEnd;
                                    message.replace("{begin}", timeBegin.toString("HH:mm"))
                                           .replace("{end}",   timeEnd.toString("HH:mm"));

                                    auto params = tbot::tgfunction("sendMessage");
                                    params->api["chat_id"] = chat->id;
                                    params->api["text"] = message;
                                    params->api["parse_mode"] = "HTML";
                                    params->messageDel = trg->hideMessageEnd ? 0 : 1*60*60 /*1 час*/;
                                    sendTgCommand(params);
                                }
                            }
                            else
                            {
                                log_verbose_m << log_format(
                                    "Trigger timelimit '%?'  skipped, it not active. Chat: %?",
                                    trg->name, chat->name());
                            }
                        }
                    }
                }
            }
    }
}

void Application::sendTgCommand(const tbot::TgParams::Ptr& params)
{
    QTimer::singleShot(params->delay, [=]()
    {
        // Не обрабатываем команды если приложение получило команду на остановку
        if (_stop)
            return;

        QString urlStr = "https://api.telegram.org";
        if (_localServer)
        {
            urlStr = "http://%1:%2";
            urlStr = urlStr.arg(_localServerAddr).arg(_localServerPort);
        }
        urlStr += "/bot%1/%2";
        urlStr = urlStr.arg(_botId).arg(params->funcName);

        QUrlQuery query;
        for (auto&& it = params->api.cbegin(); it != params->api.cend(); ++it)
            query.addQueryItem(it.key(), it.value().toString());

        QUrl url {urlStr};
        url.setQuery(query);

        QNetworkRequest request {url};
        QNetworkReply* reply = _networkAccManager->get(request);

        chk_connect_a(reply, &QIODevice::readyRead,    this, &Application::http_readyRead);
        chk_connect_a(reply, &QNetworkReply::finished, this, &Application::http_finished);

        chk_connect_a(reply, qOverload<QNetworkReply::NetworkError>(&QNetworkReply::error),
                      this,  &Application::http_error);

        chk_connect_a(reply, &QNetworkReply::sslErrors, this, &Application::http_sslErrors);
        chk_connect_a(reply, &QNetworkReply::encrypted, this, &Application::http_encrypted);

        quint64 replyId = reinterpret_cast<quint64>(reply);
        ReplyData& rd = _httpReplyMap[replyId];

        rd.reply = reply;
        rd.replyNumer = ++httpReplyNumber;
        rd.params = params;

        auto printToLog = [&rd, &url]()
        {
            log_debug_m << log_format("Http call %? (reply id: %?). Send command: %?",
                                      rd.params->funcName, rd.replyNumer, url.toString());
        };

        if (rd.params->funcName == "getChat"
            || (rd.params->funcName == "getChatAdministrators"))
        {
            if (_printGetChat
                && rd.params->funcName == "getChat")
                printToLog();

            if (_printGetChatAdmins
                && rd.params->funcName == "getChatAdministrators")
                printToLog();
        }
        else
            printToLog();
    });
}

void Application::httpResultHandler(const ReplyData& rd)
{
    if (rd.params->funcName == "getMe")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
        {
            log_error_m << "Failed parsing response for function 'getMe'"
                        << ". Re-call 'getMe' through one minute";
            QTimer::singleShot(60*1000 /*1 мин*/, this, &Application::startRequest);
            return;
        }
        if (!httpResult.ok)
        {
            log_error_m << "Failed result for function 'getMe'"
                        << ". Re-call 'getMe' through one minute";
            QTimer::singleShot(60*1000 /*1 мин*/, this, &Application::startRequest);
            return;
        }

        tbot::GetMe_Result result;
        if (!result.fromJson(rd.data))
        {
            log_error_m << "Failed parsing data for function 'getMe'"
                        << ". Re-call 'getMe' through one minute";
            QTimer::singleShot(60*1000 /*1 мин*/, this, &Application::startRequest);
            return;
        }

        _botUserId = result.user.id;
        log_info_m << "--- Bot account info ---";
        log_info_m << result.user.first_name
                   << " (@" << result.user.username << ", " << _botUserId << ")";
        log_info_m << "---";

        int procCount = 1;
        config::base().getValue("bot.processing_count", procCount);

        _commandPrefix.clear();
        config::base().getValue("bot.command_prefix", _commandPrefix);

        _commandPrefixShort.clear();
        config::base().getValue("bot.command_prefix_short", _commandPrefixShort);

        for (int i = 0; i < procCount; ++i)
        {
            tbot::Processing* p = new tbot::Processing;
            if (!p->init(_botUserId))
            {
                log_error_m << "Failed init 'tbot::Processing' instance"
                            << ". Program will be stopped";
                Application::stop();
                return;
            }
            chk_connect_q(p, &tbot::Processing::sendTgCommand,
                          this, &Application::sendTgCommand);

            chk_connect_q(p, &tbot::Processing::reportSpam,
                          this, &Application::reportSpam);

            //chk_connect_q(p, &tbot::Processing::resetSpam,
            //              this, &Application::resetSpam);

            chk_connect_q(p, &tbot::Processing::reloadGroup,
                          this, &Application::reloadGroup);

            chk_connect_q(p, &tbot::Processing::antiRaidUser,
                          this, &Application::antiRaidUser);

            chk_connect_q(p, &tbot::Processing::antiRaidMessage,
                          this, &Application::antiRaidMessage);

            chk_connect_q(p, &tbot::Processing::restrictNewUser,
                          this, &Application::restrictNewUser);

            chk_connect_q(p, &tbot::Processing::adjacentMessageDel,
                          this, &Application::adjacentMessageDel);

            chk_connect_d(&config::observerBase(), &config::ObserverBase::changed,
                          p, &tbot::Processing::reloadConfig)

            _procList.add(p);
        }
        for (tbot::Processing* p : _procList)
            p->start();

        reloadConfig();
    }
    else if (rd.params->funcName == "getChat")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        // Обработка основного сообщения для "getChat"
        if (rd.params->bio.userId == 0)
        {
            qint64 chatId = rd.params->api["chat_id"].toLongLong();
            tbot::GroupChat::List chats = tbot::groupChats();
            lst::FindResult fr = chats.findRef(chatId);

            if (httpResult.ok)
            {
                tbot::GetChat_Result result;
                if (result.fromJson(rd.data))
                {
                    if (result.chat.type == "group"
                        || result.chat.type == "supergroup")
                    {
                        // Корректируем наименование чата
                        if (fr.success())
                        {
                            QString username = result.chat.username.trimmed();
                            if (!username.isEmpty())
                            {
                                chats[fr.index()].setName(username);

                                // Необязательно здесь пересоздавать список групп,
                                // так как меняется только имя группы
                                // tbot::groupChats(&chats);
                            }
                        }
                        auto params = tbot::tgfunction("getChatAdministrators");
                        params->api["chat_id"] = chatId;
                        params->isBotCommand = rd.params->isBotCommand;
                        sendTgCommand(params);
                    }
                    else
                    {
                        // Удаляем из списка чатов неподдерживаемый чат
                        if (fr.success())
                        {
                            chats.remove(fr.index());
                            tbot::groupChats(&chats);
                            log_verbose_m << log_format(
                                "Removed unsupported chat type '%?' (chat_id: %?)",
                                result.chat.type, chatId);
                        }
                    }
                }
            }
            else
            {
                // Удаляем из списка чатов неподдерживаемый чат
                if (fr.success())
                {
                    chats.remove(fr.index());
                    tbot::groupChats(&chats);
                    log_verbose_m << log_format(
                        "Removed unavailable chat from chats list (chat_id: %?)",
                        chatId);
                }
            }
        }
        // Обработка сообщения с BIO
        else if (rd.params->bio.userId > 0)
        {
            if (httpResult.ok && !httpResult.result.isEmpty())
            {
                tbot::MessageData::Ptr msgData {tbot::MessageData::Ptr::create()};
                msgData->bio = rd.params->bio;
                msgData->isNewUser = rd.params->isNewUser;
                msgData->update.update_id = rd.params->bio.updateId;

                // Конструирование BIO сообщения
                tbot::UserBio userBio;
                if (userBio.fromJson(httpResult.result))
                {
                    tbot::User::Ptr user {new tbot::User};
                    user->id = rd.params->bio.userId;
                    user->first_name = userBio.first_name;
                    user->last_name  = userBio.last_name;
                    user->username   = userBio.username;

                    tbot::Chat::Ptr chat {new tbot::Chat};
                    chat->id = rd.params->bio.chatId;

                    tbot::Message::Ptr message {new tbot::Message};

                    message->message_id = rd.params->bio.messageId;
                    message->media_group_id = rd.params->bio.mediaGroupId;
                    message->text = userBio.bio;
                    message->from = user;
                    message->chat = chat;

                    if (userBio.business_location)
                        message->text += " " + userBio.business_location->address;

                    msgData->update.message = message;

                    //log_verbose_m << "Emulation BIO message: " << msgData->update.toJson();

                    sendToProcessing(msgData);
                }
            }
            else
                log_error_m << "Failed call function 'getChat' to get user BIO";
        }
    }
    else if (rd.params->funcName == "sendMessage")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        if (!httpResult.ok)
            return;

        // Удаляем служебные сообщения бота
        tbot::SendMessage_Result result;
        if ((rd.params->messageDel >= 0) && result.fromJson(rd.data))
        {
            qint64 chatId = result.message.chat->id;
            qint64 userId = result.message.from->id;
            qint32 messageId = result.message.message_id;

            if (_botUserId == userId)
            {
                if (rd.params->messageDel <= 10 /*сек*/)
                {
                    auto params = tbot::tgfunction("deleteMessage");
                    params->api["chat_id"] = chatId;
                    params->api["message_id"] = messageId;
                    params->delay = qMax(rd.params->messageDel*1000, 200 /*0.2 сек*/);
                    sendTgCommand(params);
                }
                else
                {
                    data::DeleteDelay dd;
                    dd.chatId = chatId;
                    dd.messageId = messageId;

                    QDateTime deleteTime = QDateTime::currentDateTime();
                    deleteTime = deleteTime.addSecs(rd.params->messageDel);
                    dd.deleteTime = deleteTime.toMSecsSinceEpoch();

                    _deleteDelays.append(dd);
                    updateBotCommands(delete_delay);
                }
            }
        }
    }
    else if (rd.params->funcName == "getChatAdministrators")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        if (!httpResult.ok)
            return;

        qint64 chatId = rd.params->api["chat_id"].toLongLong();
        tbot::GroupChat::List chats = tbot::groupChats();
        if (tbot::GroupChat* chat = chats.findItem(&chatId))
        {
            tbot::GetChatAdministrators_Result result;
            if (result.fromJson(rd.data))
            {
                QSet<qint64> adminIds;
                QSet<qint64> ownerIds;
                QStringList  adminNames;
                tbot::ChatMemberAdministrator::Ptr botInfo;
                for (const tbot::ChatMemberAdministrator::Ptr& item : result.items)
                    if (item->user)
                    {
                        adminIds.insert(item->user->id);
                        if (!item->user->username.isEmpty() && !item->user->is_bot)
                            adminNames.append(item->user->username);

                        if (item->status == "creator")
                            ownerIds.insert(item->user->id);

                        if (item->user->id == _botUserId)
                            botInfo = item;
                    }

                chat->setAdminIds(adminIds);
                chat->setOwnerIds(ownerIds);
                chat->setAdminNames(adminNames);
                chat->setBotInfo(botInfo);

                log_info_m << log_format("Group chat info updated: %?/%?",
                                         chat->name(), chatId);

                if (rd.params->isBotCommand)
                {
                    auto params = tbot::tgfunction("sendMessage");
                    params->api["chat_id"] = chatId;
                    params->api["text"] = u8"Информация о группе обновлена";
                    params->api["parse_mode"] = "HTML";
                    sendTgCommand(params);
                }
            }
        }
    }
    else if (rd.params->funcName == "banChatMember"
             || rd.params->funcName == "restrictChatMember")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        qint64 chatId = rd.params->api["chat_id"].toLongLong();
        qint64 userId = rd.params->api["user_id"].toLongLong();

        tbot::GroupChat::List chats = tbot::groupChats();
        tbot::GroupChat* chat = chats.findItem(&chatId);

        lst::FindResult frSpmr = _spammers.findRef(qMakePair(chatId, userId));

        { //Block for alog::Line
            alog::Line logLine = httpResult.ok ? log_verbose_m : log_error_m;

            if (rd.params->funcName == "banChatMember")
            {
                if (httpResult.ok)
                    logLine << "User is banned";
                else
                    logLine << "Failed ban user";
            }
            else
            {
                if (httpResult.ok)
                    logLine << "User is restricted";
                else
                    logLine << "Failed restrict user";
            }

            logLine << ", first/last/uname/id: ";

            if (frSpmr.success())
            {
                Spammer* spammer = _spammers.item(frSpmr.index());
                logLine << spammer->user->first_name;

                //logLine << "/";
                //if (!spammer->user->last_name.isEmpty())
                //    logLine << spammer->user->last_name;
                logLine << "/" << spammer->user->last_name;

                //logLine << "/";
                //if (!spammer->user->username.isEmpty())
                //    logLine << "@" << spammer->user->username;
                logLine << "/@" << spammer->user->username;
            }
            else
                logLine << "//";

            logLine << "/" << userId;

            if (chat)
                logLine << ". Chat name/id: " << chat->name() << "/" << chatId;
            else
                logLine << ". Chat id: " << chatId;

            if (httpResult.ok)
            {
                if (rd.params->funcName == "restrictChatMember")
                {
                    uint untilDate = rd.params->api["until_date"].toUInt();
                    logLine << ". Restricted to " << QDateTime::fromTime_t(untilDate);
                }
            }
            else
            {
                logLine << ". Perhaps bot has not rights to block/restric users";
                return;
            }
        }
        if (frSpmr.success() && (rd.params->funcName == "banChatMember"))
            _spammers.remove(frSpmr.index());
    }
}

void Application::reportSpam(qint64 chatId, const tbot::User::Ptr& user)
{
    auto times = [](const QList<std::time_t>& spamTimes) -> QString
    {
        QString tm;
        for (std::time_t t : spamTimes)
            tm += QString::number(qint64(t)) + " ";
        return tm.trimmed();
    };

    log_debug_m << "Report spam --- begin ---";
    for (Spammer* spammer : _spammers)
    {
        log_debug_m << log_format(
            "Report spam Id chat/spammer: %?/%?. Times: [%?]",
            spammer->chatId, spammer->user->id, times(spammer->spamTimes));
    }
    log_debug_m << "Report spam ---";

    if (chatId == 0)
    {
        // Выходим после вывода в лог текущего списка спамеров
        return;
    }

    if (user.empty())
    {
        log_error_m << "Function reportSpam(), param 'user' is empty";
        return;
    }

    log_debug_m << log_format("Report spam (0) Id chat/spammer: %?/%?",
                              chatId, user->id);

    tbot::GroupChat::List chats = tbot::groupChats();

    auto userLogInfo = [](alog::Line& logLine, const tbot::User::Ptr& user,
                          tbot::GroupChat* chat)
    {
        //QString username;
        //if (user && !user->username.isEmpty())
        //    username = "@" + user->username;

        if (user)
            logLine << log_format(
                ". User first/last/uname/id: %?/%?/@%?/%?",
                user->first_name, user->last_name, user->username, user->id);

        if (chat)
            logLine << log_format(
                ". Chat name/id: %?/%?", chat->name(), chat->id);
    };

    // Удаляем штрафы за спам если они были более двух суток назад, предпола-
    // гаем, что пользователь сделал это ненамеренно и за последующие  сутки
    // больше не спамил
    for (int i = 0; i < _spammers.count(); ++i)
    {
        Spammer* spammer = _spammers.item(i);
        std::time_t curTime = std::time(nullptr);
        for (int j = 0; j < spammer->spamTimes.count(); ++j)
        {
            const int64_t elapsedTime = curTime - spammer->spamTimes.at(j);
            if (qAbs(elapsedTime) > 2*24*60*60 /*двое суток*/)
            {
                tbot::GroupChat* chat = chats.findItem(&spammer->chatId);
                log_debug_m << log_format(
                    "Report spam (4) Id chat/spammer: %?/%?. Times: [%?]",
                    (chat ? chat->id : qint64(-1)), spammer->user->id,
                    times(spammer->spamTimes));

                { //Block for alog::Line
                    alog::Line logLine =
                        log_debug_m << "Spam penalty has expired (2 day)";
                    userLogInfo(logLine, spammer->user, chat);
                    logLine << ". Time: " << QString::number(spammer->spamTimes.at(j));
                }
                spammer->spamTimes.removeAt(j--);
            }
        }

        // Удаляем из списка спамеров без штрафов
        if (spammer->spamTimes.isEmpty())
        {
            _spammers.remove(i--);
            continue;
        }
    }

    if (tbot::GroupChat* chat = chats.findItem(&chatId))
    {
        QSet<qint64> ownerIds = chat->ownerIds();
        if (ownerIds.contains(user->id))
        {
            alog::Line logLine =
                log_verbose_m << "Owner of chat cannot receive penalty";
            userLogInfo(logLine, user, chat);
            return;
        }

        QSet<qint64> adminIds = chat->adminIds();
        if (adminIds.contains(user->id))
        {
            alog::Line logLine =
                log_verbose_m << "Admin of chat cannot receive penalty";
            userLogInfo(logLine, user, chat);
            return;
        }
    }

    // Ограничение пользователя
    auto restrictUser = [&](Spammer* spammer)
    {
        tbot::GroupChat* chat = chats.findItem(&spammer->chatId);
        if (chat == nullptr)
            return;

        if (chat->userSpamLimit > 0
            && spammer->spamTimes.count() >= chat->userSpamLimit)
        {
            // Не делаем проверку на ограничение пользователя,
            // так как на следующем шаге он будет заблокирован
            return;
        }

        if (chat->userRestricts.isEmpty())
            return;

        int restrictIndex = std::min(spammer->spamTimes.count(),
                                     chat->userRestricts.count()) - 1;
        qint64 restrictTime = chat->userRestricts[restrictIndex] * 60;
        if (restrictTime > 0)
        {
            log_verbose_m << log_format(
                "Report spam (8) Id chat/user: %?/%?. Times: [%?]",
                chat->id, spammer->user->id, times(spammer->spamTimes));

            log_verbose_m << log_format(
                "User restrict. Id chat/user: %?/%?. Time restrict: %? min",
                chat->id, spammer->user->id, restrictTime / 60);

            tbot::ChatPermissions chatPermissions;
            QByteArray permissions = chatPermissions.toJson();

            auto params = tbot::tgfunction("restrictChatMember");
            params->api["chat_id"] = chat->id;
            params->api["user_id"] = spammer->user->id;
            params->api["until_date"] = qint64(std::time(nullptr)) + restrictTime + 5;
            params->api["permissions"] = permissions;
            params->api["use_independent_chat_permissions"] = false;
            params->delay = 3*1000 /*3 сек*/;
            sendTgCommand(params);
        }
    };

    if (lst::FindResult fr = _spammers.findRef(qMakePair(chatId, user->id)))
    {
        Spammer* spammer = _spammers.item(fr.index());
        spammer->user = user; // Переприсваиваем, потому что параметры имени
                              // могут измениться
        spammer->spamTimes.append(std::time(nullptr));

        log_debug_m << log_format("Report spam (1) Id chat/spammer: %?/%?. Times: [%?]",
                                  chatId, spammer->user->id, times(spammer->spamTimes));
        restrictUser(spammer);
    }
    else
    {
        Spammer* spammer = _spammers.add();
        spammer->chatId = chatId;
        spammer->user = user;
        spammer->spamTimes.append(std::time(nullptr));

        _spammers.sort();

        log_debug_m << log_format("Report spam (2) Id chat/spammer: %?/%?. Times: [%?]",
                                  chatId, spammer->user->id, times(spammer->spamTimes));
        restrictUser(spammer);
    }

    for (int i = 0; i < _spammers.count(); ++i)
    {
        Spammer* spammer = _spammers.item(i);
        if (tbot::GroupChat* chat = chats.findItem(&spammer->chatId))
        {
            if (chat->userSpamLimit <= 0)
            {
                log_debug_m << log_format("Report spam (3) Id chat/spammer: %?/%?",
                                          chat->id, spammer->user->id);

                // Не удаляем спамера, так как его штрафы могут потребоваться
                // в механизме ограничения пользователя
                // _spammers.remove(i--);

                continue;
            }

            // Блокировка пользователя
            if (spammer->spamTimes.count() >= chat->userSpamLimit)
            {
                log_debug_m << log_format(
                    "Report spam (6) Id chat/spammer: %?/%?. Times: [%?]",
                    chat->id, spammer->user->id, times(spammer->spamTimes));

                log_debug_m << log_format(
                    "User ban. Id chat/spammer: %?/%?. Times: [%?]",
                    chat->id, spammer->user->id, times(spammer->spamTimes));

                tbot::ChatMemberAdministrator::Ptr botInfo = chat->botInfo();
                if (botInfo && botInfo->can_restrict_members)
                {
                    auto params = tbot::tgfunction("banChatMember");
                    params->api["chat_id"] = chat->id;
                    params->api["user_id"] = spammer->user->id;
                    params->api["until_date"] = qint64(std::time(nullptr));
                    params->api["revoke_messages"] = false;
                    params->delay = 3*1000 /*3 сек*/;
                    sendTgCommand(params);
                }
                else
                    log_error_m << "The bot does not have rights to ban user";
            }
        }
        else
        {
            log_debug_m << log_format(
                "Report spam (9) Id chat/spammer: %?/%?. Times: [%?]",
                chatId, spammer->user->id, times(spammer->spamTimes));
            _spammers.remove(i--);
        }
    }

    log_debug_m << "Report spam --- end ---";
    for (Spammer* spammer : _spammers)
    {
        log_debug_m << log_format(
            "Report spam Id chat/spammer: %?/%?. Times: [%?]",
            spammer->chatId, spammer->user->id, times(spammer->spamTimes));
    }
    log_debug_m << "Report spam ---";

    saveReportSpam();
}

void Application::resetSpam(qint64 chatId, qint64 userId)
{
    tbot::User::Ptr user;
    if (lst::FindResult fr = _spammers.findRef(qMakePair(chatId, userId)))
    {
        user = _spammers.item(fr.index())->user;
        _spammers.remove(fr.index());
        saveReportSpam();
    }
    if (user)
    {
        // Сообщение для Telegram
        QString botMsg =
            u8"Аннулированы спам-штрафы для пользователя [%1 %2](tg://user?id=%3)";
        botMsg = botMsg.arg(user->first_name).arg(user->last_name).arg(userId);

        if (!user->username.isEmpty())
        {
            QString s = QString(" (@%1/%2)").arg(user->username).arg(userId);
            botMsg += s.replace("_", "\\_");
        }
        else
            botMsg += QString(" (id: %1)").arg(userId);

        auto params = tbot::tgfunction("sendMessage");
        params->api["chat_id"] = chatId;
        params->api["text"] = botMsg;
        params->api["parse_mode"] = "Markdown";
        params->delay = 500 /*0.5 сек*/;
        sendTgCommand(params);

        // Сообщение в лог
        alog::Line logLine = log_verbose_m << log_format(
            "Spam penalties canceled. User first/last/uname/id: %?/%?/@%?/%?",
            user->first_name, user->last_name, user->username, user->id);

        tbot::GroupChat::List chats = tbot::groupChats();
        if (tbot::GroupChat* chat = chats.findItem(&chatId))
            logLine << log_format(". Chat name/id: %?/%?", chat->name(), chat->id);
        else
            logLine << log_format(". Chat id: %?", chatId);
    }
}

void Application::antiRaidUser(qint64 chatId, const tbot::User::Ptr& user)
{
    tbot::GroupChat::List chats = tbot::groupChats();
    if (tbot::GroupChat* chat = chats.findItem(&chatId))
    {
        if (!chat->antiRaid.active)
            return;

        if (_antiRaidCache.sortState() != lst::SortState::Up)
            _antiRaidCache.sort();

        AntiRaid* antiRaid = _antiRaidCache.findItem(&chatId);
        if (!antiRaid)
        {
            antiRaid = _antiRaidCache.add();
            antiRaid->chatId = chatId;
            _antiRaidCache.sort();
        }

        tbot::User::List* users = &antiRaid->usersTmp;
        if (chat->antiRaidTurnOn)
            users = &antiRaid->usersBan;

        if (users->sortState() != lst::SortState::Up)
            users->sort();

        lst::FindResult fr = users->findRef(user->id);
        if (fr.failed())
        {
            if (chat->antiRaidTurnOn)
                log_verbose_m << log_format(
                    "Chat: %?. Anti-Raid mode is active, user %?/%?/@%?/%? added to ban list",
                    chat->name(), user->first_name, user->last_name, user->username, user->id);

            user->add_ref();
            users->addInSort(user, fr);
        }
    }
}

void Application::antiRaidMessage(qint64 chatId, qint64 userId, qint32 messageId)
{
    tbot::GroupChat::List chats = tbot::groupChats();
    if (tbot::GroupChat* chat = chats.findItem(&chatId))
    {
        if (!chat->antiRaid.active)
            return;

        if (!chat->antiRaidTurnOn)
            return;

        if (AntiRaid* antiRaid = _antiRaidCache.findItem(&chatId))
            if (tbot::User* user = antiRaid->usersBan.findItem(&userId))
            {
                log_verbose_m << log_format(
                    "Chat: %?. Anti-Raid mode is active, user %?/%?/@%?/%? in ban list"
                    ". Message %? added to remove list",
                    chat->name(), user->first_name, user->last_name, user->username,
                    user->id, messageId);

                if (!antiRaid->messageIds.contains(messageId))
                    antiRaid->messageIds.append(messageId);
            }
    }
}

void Application::restrictNewUser(qint64 chatId, qint64 userId, qint32 newUserMute)
{
    NewUser* newUser = _newUsers.findItem(&userId);
    if (newUser == nullptr)
    {
        newUser = _newUsers.add();
        newUser->userId = userId;
        _newUsers.sort();
    }

    auto restrictCmd = [this](qint64 chatId, qint64 userId, qint64 restrictTime)
    {
        tbot::ChatPermissions chatPermissions;
        QByteArray permissions = chatPermissions.toJson();

        auto params = tbot::tgfunction("restrictChatMember");
        params->api["chat_id"] = chatId;
        params->api["user_id"] = userId;
        params->api["until_date"] = qint64(std::time(nullptr)) + restrictTime + 5;
        params->api["permissions"] = permissions;
        params->api["use_independent_chat_permissions"] = false;
        //params->delay = 2*1000/*2 сек*/ * index;
        sendTgCommand(params);
    };

    if (!newUser->chatIds.contains(chatId))
    {
        newUser->chatIds.insert(chatId);
        qint64 restrictTime = 12*60*60 /*12 часов*/;
        if ((newUser->chatIds.count() == 1) && (newUserMute > 0))
        {
            restrictCmd(chatId, userId, newUserMute*60 /*newUserMute мин*/);
        }
        else if (newUser->chatIds.count() == 2)
        {
            for (qint64 chatId_ : newUser->chatIds)
                restrictCmd(chatId_, userId, restrictTime);
        }
        if (newUser->chatIds.count() > 2)
        {
            restrictTime += 6*60*60 /*6 часов*/ * (newUser->chatIds.count() - 2);
            restrictCmd(chatId, userId, restrictTime);
        }
        newUser->timer.reset();
    }
}

void Application::adjacentMessageDel(qint64 chatId, qint32 messageId)
{
    if (lst::FindResult fr = _adjacentMessages.findRef(tuple{chatId, messageId}))
        _adjacentMessages[fr.index()].deleted = true;
}

void Application::loadBotCommands()
{
    // timelimit_inactive
    QSet<qint64> timelimitInactiveChats;
    config::state().getValue("timelimit_inactive.chats", timelimitInactiveChats);
    tbot::setTimelimitInactiveChats(timelimitInactiveChats);

    // user_trigger
    YamlConfig::Func loadFunc = [this](YamlConfig* conf, YAML::Node& nodes, bool)
    {
        for (const YAML::Node& node : nodes)
        {
            qint64 chatId = 0;
            conf->getValue(node, "id", chatId);

            data::UserTrigger* userTrgList = _userTriggers.add();
            userTrgList->chatId = chatId;

            YamlConfig::Func loadFunc2 = [userTrgList](YamlConfig* conf, YAML::Node& nodes, bool)
            {
                for (const YAML::Node& node : nodes)
                {
                    data::UserTrigger::Item* itemTrg = userTrgList->items.add();
                    conf->getValue(node, "keys", itemTrg->keys);
                    conf->getValue(node, "text", itemTrg->text);
                }
                userTrgList->items.sort();
                return true;
            };
            conf->getValue(node, "triggers", loadFunc2, false);
        }
        _userTriggers.sort();
        return true;
    };
    _userTriggers.clear();
    config::state().getValue("user_trigger.chats", loadFunc, false);

    // delete_delay
    YamlConfig::Func loadFunc2 = [this](YamlConfig* conf, YAML::Node& nodes, bool)
    {
        for (const YAML::Node& node : nodes)
        {
            data::DeleteDelay dd;
            conf->getValue(node, "chat_id",     dd.chatId);
            conf->getValue(node, "message_id",  dd.messageId);
            conf->getValue(node, "delete_time", dd.deleteTime);
            _deleteDelays.append(dd);
        }
        return true;
    };
    _deleteDelays.clear();
    config::state().getValue("delete_delay.items", loadFunc2, false);

    // user_join_time
    QString stateFile;
    config::base().getValue("user_join_time.file", stateFile);

    auto loadFunc3 = [stateFile]()
    {
        QFile userJoinFile {stateFile};
        if (!userJoinFile.exists())
        {
            log_warn_m << "User-Join state file not exists " << stateFile;
            return;
        }

        if (!userJoinFile.open(QIODevice::ReadOnly))
        {
            log_error_m << "Failed open User-Join state file in read-only mode"
                        << ". File: " << stateFile;
            return;
        }

        QByteArray ba = userJoinFile.readAll();
        userJoinFile.close();

        data::UserJoinTimeSerialize serialize;
        if (!serialize.fromJson(ba))
        {
            log_error_m << "Failed deserialize User-Join data from file " << stateFile;
            return;
        }
        tbot::userJoinTimes().listSwap(serialize.items);
        tbot::userJoinTimes().resetChangeFlag();
    };
    loadFunc3();
}

void Application::saveBotCommands(UpdateBotSection section, qint64 timemark)
{
    if (section == timelimit_inactive)
    {
        config::state().setValue("timelimit_inactive.timemark", timemark);
        config::state().setValue("timelimit_inactive.chats", tbot::timelimitInactiveChats());
    }
    else if (section == user_trigger)
    {
        config::state().setValue("user_trigger.timemark", timemark);

        YamlConfig::Func saveFunc = [this](YamlConfig* conf, YAML::Node& node, bool)
        {
            for (data::UserTrigger* userTrgList : _userTriggers)
            {
                YAML::Node chatTrg;
                conf->setValue(chatTrg, "id", userTrgList->chatId);

                YamlConfig::Func saveFunc2 = [userTrgList](YamlConfig* conf, YAML::Node& node, bool)
                {
                    for (data::UserTrigger::Item* itemTrg : userTrgList->items)
                    {
                        YAML::Node trg;
                        conf->setValue(trg, "keys", itemTrg->keys);
                        conf->setValue(trg, "text", itemTrg->text);
                        node.push_back(trg);
                    }
                    return true;
                };
                conf->setValue(chatTrg, "triggers", saveFunc2);
                node.push_back(chatTrg);
            }
            return true;
        };
        config::state().remove("user_trigger.chats");
        config::state().setValue("user_trigger.chats", saveFunc);
    }
    else if (section == delete_delay)
    {
        config::state().setValue("delete_delay.timemark", timemark);

        YamlConfig::Func saveFunc = [this](YamlConfig* conf, YAML::Node& node, bool)
        {
            for (const data::DeleteDelay& dd : _deleteDelays)
            {
                YAML::Node n;
                conf->setValue(n, "chat_id",     dd.chatId);
                conf->setValue(n, "message_id",  dd.messageId);
                conf->setValue(n, "delete_time", dd.deleteTime);
                node.push_back(n);
            }
            return true;
        };
        config::state().remove("delete_delay.items");
        config::state().setValue("delete_delay.items", saveFunc);
    }
    else if (section == user_join_time)
    {
        config::state().setValue("user_join_time.timemark", timemark);

        QString stateFile;
        config::base().getValue("user_join_time.file", stateFile);

        QFile file {stateFile};
        if (!file.open(QIODevice::WriteOnly))
        {
            log_error_m << "Failed open User-Join state file in write mode"
                        << ". File: " << stateFile;
            return;
        }

        data::UserJoinTimeSerialize serialize;
        serialize.items = tbot::userJoinTimes().list();
        QByteArray ba = serialize.toJson();
        file.write(ba);
        file.close();
    }

    // Сохранение состояния происходит по таймеру _configStateTimerId
    // config::state().saveFile()
}

void Application::updateBotCommands(UpdateBotSection section)
{
    qint64 timemark = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // Сохраняем состояние секции/команды
    saveBotCommands(section, timemark);

    if ((section == timelimit_inactive) && _masterMode)
    {
        data::TimelimitSync timelimitSync;
        timelimitSync.timemark = timemark;
        timelimitSync.chats = tbot::timelimitInactiveChats();

        // Отправляем событие с измененными timelimit
        Message::Ptr m = createJsonMessage(timelimitSync, {Message::Type::Event});
        tcp::listener().send(m);
    }
    else if (section == user_trigger)
    {
       data::UserTriggerSync userTriggerSync;
       userTriggerSync.timemark = timemark;
       userTriggerSync.triggers.assign(_userTriggers);

       // Отправляем событие с измененными user_trigger
       Message::Ptr m = createJsonMessage(userTriggerSync, {Message::Type::Event});
       tcp::listener().send(m);
    }
    else if (section == delete_delay)
    {
        data::DeleteDelaySync deleteDelaySync;
        deleteDelaySync.timemark = timemark;
        deleteDelaySync.items = _deleteDelays;

        // Отправляем событие с измененными delete_delay
        Message::Ptr m = createJsonMessage(deleteDelaySync, {Message::Type::Event});
        tcp::listener().send(m);
    }
    else if (section == user_join_time)
    {
        data::UserJoinTimeSync userJoinTimeSync;
        userJoinTimeSync.timemark = timemark;
        userJoinTimeSync.items = tbot::userJoinTimes().list();

        // Отправляем событие с измененными user_join_time
        Message::Ptr m = createJsonMessage(userJoinTimeSync, {Message::Type::Event});
        tcp::listener().send(m);
    }
}

void Application::sendToProcessing(const tbot::MessageData::Ptr& msgData)
{
    tbot::Message::Ptr message = (msgData->update.message)
                                 ? msgData->update.message
                                 : msgData->update.edited_message;

    if (message && message->chat && message->from)
    {
        //const qint64 chatId = message->chat->id;
        //const qint64 userId = message->from->id;
        //const qint32 messageId = message->message_id;
        //tuple find {chatId, userId, messageId};

        simple_ptr<AdjacentMessage> item {new AdjacentMessage};
        item->chatId = message->chat->id;
        item->userId = message->from->id;
        item->messageId = message->message_id;
        item->mediaGroupId = message->media_group_id;

        lst::FindResult fr = _adjacentMessages.find(item.get());
        if (fr.failed())
        {
            item->text = message->text.trimmed();
            QString caption = message->caption.trimmed();
            if (!caption.isEmpty())
            {
                if (item->text.isEmpty())
                    item->text = caption;
                else
                    item->text = caption + '\n' + item->text;
            }
            _adjacentMessages.addInSort(item.release(), fr);
        }
        if (_adjacentMessages.sortState() != lst::SortState::Up)
            _adjacentMessages.sort();
    }

    if (!_masterMode)
    {
        bool slaveActive = true;
        if (_slaveSocket && _slaveSocket->isConnected())
        {
            if (_masterStartTime.isValid())
            {
                if (_masterStartTime.secsTo(QDateTime::currentDateTimeUtc()) > 5*60 /*5 мин*/)
                    slaveActive = false;
            }
            else
                log_error_m << "Master-bot start time value is invalid";
        }
        if (slaveActive)
        {
            // Slave mode
            tbot::Processing::addUpdate(msgData);
        }
        else
            log_verbose_m << "Bot in slave passive mode, message processing skipped";
    }
    else
    {
        // Master mode
        tbot::Processing::addUpdate(msgData);
    }
}

bool Application::botCommand(const tbot::MessageData::Ptr& msgData)
{
    if (!_masterMode && _slaveSocket && _slaveSocket->isConnected())
    {
        log_verbose_m << "Bot in slave passive mode, bot-command processing skipped";
        return false;
    }

    tbot::Message::Ptr message = (msgData->update.message)
                                 ? msgData->update.message
                                 : msgData->update.edited_message;
    if (message.empty()
        || message->chat.empty()
        || message->from.empty())
    {
        return false;
    }

    const qint64 chatId = message->chat->id;
    const qint64 userId = message->from->id;
    const qint32 messageId = message->message_id;

    tbot::GroupChat::List chats = tbot::groupChats();
    tbot::GroupChat* chat = chats.findItem(&chatId);

    if (chat == nullptr)
        return false;

    auto deleteMessage = [this, chatId, messageId]()
    {
        auto params = tbot::tgfunction("deleteMessage");
        params->api["chat_id"] = chatId;
        params->api["message_id"] = messageId;
        params->delay = 200 /*0.2 сек*/;
        sendTgCommand(params);
    };

    auto sendMessage = [this, chatId](QString msg, bool messageDel = true,
                                      qint32 replyMsgId = -1)
    {
        // Исключаем из замены html-теги <b> </b> <i> </i> <s> </s> <u> </u>
        static QRegularExpression re1 {R"(<(?!([bisu]>|\/[bisu]>)))"};
        static QRegularExpression re2 {R"((?<!(\/[bisu]|<[bisu]))>)"};

        msg.replace(re1, "&#60;"); // <
        msg.replace(re2, "&#62;"); // >
        msg.replace("+", "&#43;"); // +

        auto params = tbot::tgfunction("sendMessage");
        params->api["chat_id"] = chatId;
        params->api["text"] = msg;
        params->api["parse_mode"] = "HTML";
        params->delay = 1.5*1000 /*1.5 сек*/;
        params->messageDel = (messageDel) ? 0 : -1;

        if (replyMsgId > 0)
            params->api["reply_to_message_id"] = replyMsgId;

        sendTgCommand(params);
    };

    static QSet<QString> vaCmdShorts {u8"проверь", u8"проверка", u8"проверить"};

    bool isBotCommand = false;
    for (const tbot::MessageEntity& entity : message->entities)
        if (entity.type == "bot_command")
        {
            isBotCommand = true;
            break;
        }

    //--- Короткая запись для вызова пользовательского триггера ---
    if (!isBotCommand)
    {
        if (message->text.length() < 12)
            if (vaCmdShorts.contains(message->text.toLower().trimmed()))
            {
                // Удаляем сообщение с командой
                deleteMessage();

                tbot::Processing::addVerifyAdmin(chatId, userId, messageId + 1);
                return true;
            }

        if (data::UserTrigger* userTrgList = _userTriggers.findItem(&chatId))
        {
            QString triggerName = message->text.trimmed();
            lst::FindResult fr =
                userTrgList->items.findRef(triggerName, {lst::BruteForce::Yes});
            if (fr.success())
            {
                // Удаляем сообщение с именем триггера
                deleteMessage();

                QString text = userTrgList->items[fr.index()].text;
                tbot::Message::Ptr reply = message->reply_to_message;
                sendMessage(text, false, (reply) ? reply->message_id : qint32(-1));
                return true;
            }
        }
    }

    QString botMsg;
    for (const tbot::MessageEntity& entity : message->entities)
    {
        if (entity.type != "bot_command")
            continue;

        QString prefix = message->text.mid(entity.offset, entity.length);
        prefix.remove(QRegularExpression("@.*$"));

        if (!((prefix == _commandPrefix) || (prefix == _commandPrefixShort)))
            continue;

        // Команда для бота должна быть в начале сообщения
        if (entity.offset > 0)
            continue;

        // Удаляем сообщение с командой
        deleteMessage();

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

                u8"\r\n%1 verifyadmin [va]&#185; - "
                u8"Проверить принадлежность пользователя к администраторам группы;"
                u8"\r\n"

                u8"\r\n%1 reloadgroup [rg]&#185; - "
                u8"Обновить информацию о группе и её администраторах;"
                u8"\r\n"

                u8"\r\n%1 trigger [tr]&#185; (add name|del name|list) - "
                u8"Создание/удаление пользовательских триггеров;"
                u8"\r\n"

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

        //--- Команда может быть выполнена обычным пользователем ---
        if ((command == "verifyadmin") || (command == "va"))
        {
            tbot::Processing::addVerifyAdmin(chatId, userId, messageId + 1);
            return true;
        }

        if (!chat->adminIds().contains(userId))
        {
            // Тайминг сообщения 1.5 сек
            sendMessage(u8"Для управления ботом нужны права администратора");

            // Штрафуем пользователя, который пытается управлять ботом (тайминг 3 сек)
            reportSpam(chatId, message->from);
            return true;
        }

        //--- Команды могут быть выполнены только администратором ---
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
                tbot::timelimitInactiveChatsRemove(chatId);
                updateBotCommands(timelimit_inactive);

                sendMessage(u8"Триггер timelimit активирован");
            }
            else if (action == "stop")
            {
                tbot::timelimitInactiveChatsAdd(chatId);
                updateBotCommands(timelimit_inactive);

                sendMessage(u8"Триггер timelimit деактивирован");
            }
            else if (action == "status")
            {
                for (tbot::Trigger* trigger : chat->triggers)
                {
                    tbot::TriggerTimeLimit* trg = dynamic_cast<tbot::TriggerTimeLimit*>(trigger);
                    if (trg == nullptr)
                        continue;

                    botMsg = u8"Триггер: " + trigger->name;
                    if (!trg->description.isEmpty())
                        botMsg += QString(" (%1)").arg(trg->description);

                    if (tbot::timelimitInactiveChats().contains(chatId))
                        botMsg += u8"\r\nСостояние: не активен";
                    else
                        botMsg += u8"\r\nСостояние: активен";

                    botMsg += u8"\r\nUTC часовой пояс: " + QString::number(trg->utc);
                    botMsg += u8"\r\nРасписание:";

                    bool first = true;
                    for (const tbot::TriggerTimeLimit::Day& day : trg->week)
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
                        for (const tbot::TriggerTimeLimit::TimeRange& time : day.times)
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
            bool ok = false;
            qint64 spamUserId = 0;
            for (const QString& action : actions)
            {
                spamUserId = action.toLongLong(&ok);
                if (ok) break;
            }
            if (!ok)
            {
                log_error_m << log_format(
                    "Bot command 'spamreset'. Failed extract user id from '%?'",
                    actionLine);

                botMsg = u8"Команда spamreset. Ошибка получения "
                         u8"идентификатора пользователя";
                sendMessage(botMsg);
            }
            else
            {
                resetSpam(chatId, spamUserId);
            }
            return true;
        }
        else if ((command == "reloadgroup") || (command == "rg"))
        {
            reloadGroup(chatId, true);
            return true;
        }
        else if ((command == "trigger") || (command == "tr"))
        {
            QString action;
            if (actions.count())
                action = actions[0];

            data::UserTrigger* userTrgList = _userTriggers.findItem(&chatId);
            if (userTrgList == nullptr)
            {
                userTrgList = _userTriggers.add();
                userTrgList->chatId = chatId;
                _userTriggers.sort();
            }

            lst::FindResult fr =
                userTrgList->items.findRef(action, {lst::BruteForce::Yes});
            if (fr.success())
            {
                QString text = userTrgList->items[fr.index()].text;
                sendMessage(text, false);
                return true;
            }

            if (action != "add"
                && action != "del"
                && action != "list")
            {
                botMsg = (action.isEmpty())
                    ? u8"Для команды trigger требуется указать действие."
                    : u8"Команда trigger не используется с указанным действием: %1.";

                botMsg += u8"\r\nДопустимые действия: add name/del name/list"
                          u8"\r\nПример: %2 trigger add карта";
                botMsg = botMsg.arg(action).arg(_commandPrefix);

                sendMessage(botMsg);
                return true;
            }

            if (action == "add")
            {
                if (actions.count() < 2)
                {
                    botMsg = u8"Не указано имя триггера";
                    sendMessage(botMsg);
                    return true;
                }

                tbot::Message::Ptr reply = message->reply_to_message;
                if (reply.empty())
                {
                    botMsg = u8"Не указано сообщение для пользовательского триггера";
                    sendMessage(botMsg);
                    return true;
                }

                if (chatId != reply->chat->id)
                {
                    log_error_m << "Bot command 'trigger' fail: chatId != reply->chat->id";
                    return false;
                }

                QStringList triggerKeys = actions[1].split(QChar(','), QString::SkipEmptyParts);
                triggerKeys.sort(Qt::CaseInsensitive);
                for (const QString& key : triggerKeys)
                {
                    if (vaCmdShorts.contains(key.toLower().trimmed()))
                    {
                        botMsg = u8"Слова <i>%1</i> зарезервированы, "
                                 u8"их нельзя использоваться в качестве имени триггера";
                        QStringList sl {vaCmdShorts.toList()}; sl.sort();
                        botMsg = botMsg.arg(sl.join(QChar(' ')));

                        sendMessage(botMsg);
                        return true;
                    }
                    if (userTrgList->items.findRef(key, {lst::BruteForce::Yes}))
                    {
                        botMsg = u8"Пользовательский триггер '%1' уже существует";
                        botMsg = botMsg.arg(key);

                        sendMessage(botMsg);
                        return true;
                    }
                }

                data::UserTrigger::Item* userTrgItem = userTrgList->items.add();
                userTrgItem->keys = triggerKeys;
                userTrgItem->text = reply->text;
                userTrgList->items.sort();

                updateBotCommands(user_trigger);

                botMsg = u8"Добавлен пользовательский триггер '%1'";
                botMsg = botMsg.arg(triggerKeys.join(QChar(',')));

                sendMessage(botMsg);
            }
            else if (action == "del")
            {
                if (actions.count() < 2)
                {
                    botMsg = u8"Не указано имя триггера";
                    sendMessage(botMsg);
                    return true;
                }

                QString triggerName = actions[1];
                lst::FindResult fr =
                    userTrgList->items.findRef(triggerName, {lst::BruteForce::Yes});
                if (fr.failed())
                {
                    botMsg = u8"Не удалось удалить пользовательский триггер."
                             u8"\r\nТриггер '%1' не существует";
                    botMsg = botMsg.arg(triggerName);

                    sendMessage(botMsg);
                    return true;
                }

                QString delTrgKeys = userTrgList->items[fr.index()].keys.join(QChar(','));

                userTrgList->items.remove(fr.index());
                updateBotCommands(user_trigger);

                botMsg = u8"Пользовательский триггер '%1' удален";
                botMsg = botMsg.arg(delTrgKeys);

                sendMessage(botMsg);
            }
            else if (action == "list")
            {
                botMsg = u8"Список пользовательских триггеров";
                for (data::UserTrigger::Item* item : userTrgList->items)
                {
                    QString str = u8"\r\n---"
                                  u8"\r\nТриггер: %1"
                                  u8"\r\nТекст: %2";

                    str = str.arg(item->keys.join(QChar(',')) , item->text);
                    botMsg += str;

                    // При выводе списка триггеров исключаем html-форматирование.
                    // Если  в тегах  html-форматирования  будут  ошибки  список
                    // триггеров может не отобразиться
                    botMsg.replace("<", "&#60;");
                    botMsg.replace(">", "&#62;");
                }
                sendMessage(botMsg);
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

void Application::loadReportSpam()
{
    QString stateFile;
    config::base().getValue("report_spam.file", stateFile);

    if (!QFile::exists(stateFile))
    {
        log_warn_m << "Report-spam state file not exists " << stateFile;
        return;
    }

    YamlConfig yconfig;
    if (!yconfig.readFile(stateFile.toStdString()))
    {
        log_error_m << "Report-spam state file structure is corrupted";
        return;
    }

    YamlConfig::Func loadFunc = [this](YamlConfig* conf, YAML::Node& nodes, bool)
    {
        for (const YAML::Node& node : nodes)
        {
            Spammer* spammer = _spammers.add();
            conf->getValue(node, "chat_id", spammer->chatId);

            QString user;
            conf->getValue(node, "user", user);
            spammer->user = tbot::User::Ptr(new tbot::User);
            spammer->user->fromJson(user.toUtf8());

            conf->getValue(node, "spam_times", spammer->spamTimes);
        }
        _spammers.sort();
        return true;
    };
    yconfig.getValue("spammers", loadFunc);
}

void Application::saveReportSpam()
{
    QString stateFile;
    config::base().getValue("report_spam.file", stateFile);

    YamlConfig::Func saveFunc = [this](YamlConfig* conf, YAML::Node& node, bool)
    {
        for (Spammer* spammer : _spammers)
        {
            QString user {spammer->user->toJson()};

            YAML::Node spmr;
            conf->setValue(spmr, "chat_id", spammer->chatId);
            conf->setValue(spmr, "user", user);
            conf->setValue(spmr, "spam_times", spammer->spamTimes);

            node.push_back(spmr);
        }
        return true;
    };

    YamlConfig yconfig;
    yconfig.setValue("spammers", saveFunc);
    yconfig.saveFile(stateFile.toStdString());
}

void Application::loadAntiRaidCache()
{
    QString stateFile;
    config::base().getValue("anti_raid.file", stateFile);

    if (!QFile::exists(stateFile))
    {
        log_warn_m << "Anti-raid state file not exists " << stateFile;
        return;
    }

    YamlConfig yconfig;
    if (!yconfig.readFile(stateFile.toStdString()))
    {
        log_error_m << "Anti-raid state file structure is corrupted";
        return;
    }

    YamlConfig::Func loadFunc = [this](YamlConfig* conf, YAML::Node& nodes, bool)
    {
        for (const YAML::Node& node : nodes)
        {
            AntiRaid* antiRaid = _antiRaidCache.add();
            conf->getValue(node, "chat_id", antiRaid->chatId);

            qint64 deactiveTime = 0;
            conf->getValue(node, "deactive_time", deactiveTime);
            antiRaid->deactiveTime = QDateTime::fromMSecsSinceEpoch(deactiveTime);

            QList<QString> users;
            conf->getValue(node, "users", users);
            for (const QString& userStr : users)
            {
                tbot::User* user = antiRaid->usersBan.add();
                user->fromJson(userStr.toUtf8());
            }
            antiRaid->usersBan.sort();

            antiRaid->messageIds.clear();
            conf->getValue(node, "messages", antiRaid->messageIds);
        }
        _antiRaidCache.sort();
        return true;
    };
    yconfig.getValue("anti_raid", loadFunc);
}

void Application::saveAntiRaidCache()
{
    QString stateFile;
    config::base().getValue("anti_raid.file", stateFile);

    YamlConfig::Func saveFunc = [this](YamlConfig* conf, YAML::Node& node, bool)
    {
        for (AntiRaid* antiRaid : _antiRaidCache)
        {
            QDateTime currentTime = QDateTime::currentDateTimeUtc();

            if (antiRaid->usersBan.empty()
                && antiRaid->messageIds.empty()
                && currentTime > antiRaid->deactiveTime)
                continue;

            YAML::Node araid;
            conf->setValue(araid, "chat_id", antiRaid->chatId);

            qint64 deactiveTime = antiRaid->deactiveTime.toMSecsSinceEpoch();
            conf->setValue(araid, "deactive_time", deactiveTime);

            if (antiRaid->usersBan.count())
            {
                QList<QString> users;
                for (tbot::User* user : antiRaid->usersBan)
                {
                    QString u {user->toJson()};
                    users.append(u);
                }
                conf->setValue(araid, "users", users);
                conf->setNodeStyle(araid, "users", YAML::EmitterStyle::Block);
            }

            if (antiRaid->messageIds.count())
                conf->setValue(araid, "messages", antiRaid->messageIds);

            node.push_back(araid);
        }
        return true;
    };

    YamlConfig yconfig;
    yconfig.setValue("anti_raid", saveFunc);

    static bool lastDataIsEmpty {false};
    if (YAML::Node root = yconfig.node("."))
    {
        if (root.size())
        {
            lastDataIsEmpty = false;
            yconfig.saveFile(stateFile.toStdString());
        }
        else if ((root.size() == 0) && !lastDataIsEmpty)
        {
            lastDataIsEmpty = true;
            yconfig.saveFile(stateFile.toStdString());
        }
    }
}
