#include "telebot_appl.h"

#include "trigger.h"
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
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <thread>

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
    _stopTimerId = startTimer(1000);
    _slaveTimerId = startTimer(10*1000 /*10 сек*/);
    _timelimitTimerId = startTimer(15*1000 /*15 сек*/);
    _updateAdminsTimerId = startTimer(60*60*1000 /*1 час*/);

    chk_connect_a(&config::observerBase(), &config::ObserverBase::changed,
                  this, &Application::reloadConfig)

    chk_connect_a(&config::observer(), &config::Observer::changed,
                  this, &Application::reloadGroups)

    #define FUNC_REGISTRATION(COMMAND) \
        _funcInvoker.registration(command:: COMMAND, &Application::command_##COMMAND, this);

    FUNC_REGISTRATION(SlaveAuth)
    FUNC_REGISTRATION(ConfSync)
    FUNC_REGISTRATION(TimelimitSync)

    #undef FUNC_REGISTRATION

    qRegisterMetaType<ReplyData>("ReplyData");
    qRegisterMetaType<tbot::HttpParams>("tbot::HttpParams");
    qRegisterMetaType<tbot::User::Ptr>("tbot::User::Ptr");
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

    quint16 port;
    config::base().getValue("webhook.port", port);

    log_verbose_m << "Start webhook server"
                  << ". Address: " << QHostAddress::AnyIPv4
                  << ". Port: " << port;

    if (!_webhookServer->listen(QHostAddress::AnyIPv4, port))
    {
        log_error_m << "Failed start TCP-server"
                    << ". Error: " << _webhookServer->errorString();
        return false;
    }

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

    loadReportSpam();
    reportSpam(0, {}); // Выводим в лог текущий спам-список

    loadBotCommands();

    startRequest();
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
}

void Application::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == _stopTimerId)
    {
        if (_stop)
        {
            KILL_TIMER(_stopTimerId)
            KILL_TIMER(_slaveTimerId)
            KILL_TIMER(_timelimitTimerId)
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
    else if (event->timerId() == _updateAdminsTimerId)
    {
        if (_masterMode || (_slaveSocket && !_slaveSocket->isConnected()))
        {
            log_verbose_m << "Update groups config-file by timer";

            // Обновляем конфигурацию групп и список актуальных админов
            reloadGroups();
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
}

void Application::command_SlaveAuth(const Message::Ptr& message)
{
    if (!_masterMode && (message->type() == Message::Type::Answer))
    {
        if (message->execStatus() == Message::ExecStatus::Success)
        {
            log_info_m << "Success authorization on master-bot"
                       << ". Remote host: " << message->sourcePoint();

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

        // Отправляем подтверждение об успешной операции: пустое сообщение
        tcp::listener().send(answer);

        log_info_m << "Success authorization slave-bot"
                   << ". Remote host: " << message->sourcePoint();

        qint64 timemark = 0;
        config::state().getValue("timelimit_inactive.timemark", timemark);

        // Отправлем команду синхронизации timelimit
        data::TimelimitSync timelimitSync;
        timelimitSync.timemark = timemark;
        timelimitSync.chats = tbot::timelimitInactiveChats().toList();

        Message::Ptr m = createJsonMessage(timelimitSync);
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

    config::work().readFile(configFileS.toStdString());
    reloadGroups();

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
        saveBotCommands(timelimitSync.timemark);

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
            timelimitSync.chats = tbot::timelimitInactiveChats().toList();

            writeToJsonMessage(timelimitSync, answer);
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

    socket->setPeerVerifyMode(QSslSocket::VerifyNone);
    socket->setLocalCertificateChain(QList<QSslCertificate>{_sslCert});
    socket->setPrivateKey(_sslKey);
    socket->setProtocol(QSsl::TlsV1_3OrLater);
    socket->startServerEncryption();

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
        log_verbose_m << "Webhook TCP data: " << wd.data;

        if (!_masterMode)
        {
            if (_slaveSocket && !_slaveSocket->isConnected())
            {
                // Slave mode
                tbot::Processing::addUpdate(wd.data);
            }
            else
                log_verbose_m << "Bot in slave mode, message processing skipped";
        }
        else
        {
            // Master mode
            tbot::Processing::addUpdate(wd.data);
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
                                  rd.replyNumer, rd.funcName, rd.attempt);

        { //Block for alog::Line
            alog::Line logLine =
                    log_debug_m << log_format("Http answer (reply id: %?) params : ",
                                              rd.replyNumer);
            for (auto&& it = rd.params.cbegin(); it != rd.params.cend(); ++it)
                logLine << it.key() << ": " << it.value().toString() << "; ";
        }
        log_debug_m << log_format("Http answer (reply id: %?) return : %?",
                                  rd.replyNumer, rd.data);
    };

    if (rd.funcName == "getChat"
        || (rd.funcName == "getChatAdministrators"))
    {
        if ((rd.funcName == "getChat") && _printGetChat)
            printToLog();

        if ((rd.funcName == "getChatAdministrators") && _printGetChatAdministrators)
            printToLog();
    }
    else
        printToLog();

    if (rd.success)
    {
        QMetaObject::invokeMethod(this, "httpResultHandler", Qt::QueuedConnection,
                                  Q_ARG(ReplyData, rd));
    }
    else
    {
        if (rd.funcName == "deleteMessage"
            || rd.funcName == "banChatMember"
            || rd.funcName == "restrictChatMember")
        {
            if (rd.attempt == 1)
                sendTgCommand(rd.funcName, rd.params, 30*1000 /*30 сек*/, 2);

            if (rd.attempt == 2)
                sendTgCommand(rd.funcName, rd.params, 60*1000 /*60 сек*/, 3);
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
    if (_masterMode)
    {
        config::work().readFile(configFileM.toStdString());
        config::observer().removeFile(configFileS);
        config::observer().addFile(configFileM);
    }
    else
    {
        config::work().readFile(configFileS.toStdString());
        config::observer().removeFile(configFileM);
        config::observer().addFile(configFileS);
    }

    _printGetChat = true;
    config::base().getValue("print_log.get_chat", _printGetChat);

    _printGetChatAdministrators = true;
    config::base().getValue("print_log.get_chatadministrators", _printGetChatAdministrators);

    reloadBotMode();
    reloadGroups();
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

void Application::reloadGroups()
{
    if (!config::work().rereadFile())
        return;

    _getChatAdminCallCount = 0;
    tbot::globalConfigParceErrors = 0;

    { //Block for tbot::Trigger::List
        tbot::Trigger::List triggers;
        tbot::loadTriggers(triggers);
        tbot::printTriggers(triggers);
        tbot::triggers(&triggers);
    }

    { //Block for tbot::GroupChat::List
        tbot::GroupChat::List chats;
        tbot::loadGroupChats(chats);
        tbot::printGroupChats(chats);
        tbot::groupChats(&chats);
    }

    tbot::GroupChat::List chats = tbot::groupChats();
    for (int i = 0; i < chats.count(); ++i)
    {
        tbot::GroupChat* chat = chats.item(i);
        tbot::HttpParams params;
        params["chat_id"] = chat->id;

        // Команды getChat отправляются с интервалом 70  миллисекунд,  чтобы
        // уложиться в ограничение Телеграм на 30 запросов в секунду от бота.
        // Интервал 70 миллисекунд обеспечивает двукратный запас  по времени,
        // это обусловлено тем, что после вызова getChat идет вызов команды
        // getChatAdministrators
        sendTgCommand("getChat", params, 70 * i);
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
    tbot::HttpParams params;
    sendTgCommand("getMe", params);
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

                                tbot::HttpParams params;
                                params["chat_id"] = chat->id;
                                params["text"] = message;
                                params["parse_mode"] = "HTML";

                                int messageDel =
                                    trg->hideMessageBegin ? 0 : 6*60*60 /*6 часов*/;

                                sendTgCommand("sendMessage", params, 0, 1, messageDel);
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

                                tbot::HttpParams params;
                                params["chat_id"] = chat->id;
                                params["text"] = message;
                                params["parse_mode"] = "HTML";

                                int messageDel =
                                    trg->hideMessageEnd ? 0 : 1*60*60 /*1 час*/;

                                sendTgCommand("sendMessage", params, 0, 1, messageDel);
                            }
                        }
                    }
                }
            }
    }
}

void Application::sendTgCommand(const QString& funcName, const tbot::HttpParams& params,
                                int delay, int attempt, int messageDel)
{
    QTimer::singleShot(delay, [=]()
    {
        QString urlStr = "https://api.telegram.org/bot%1/%2";
        urlStr = urlStr.arg(_botId).arg(funcName);

        QUrlQuery query;
        for (auto&& it = params.cbegin(); it != params.cend(); ++it)
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
        rd.funcName = funcName;
        rd.params = params;
        rd.attempt = attempt;
        rd.messageDel = messageDel;

        auto printToLog = [&rd, &url]()
        {
            log_debug_m << log_format("Http call %? (reply id: %?). Send command: %?",
                                      rd.funcName, rd.replyNumer, url.toString());
        };

        if (rd.funcName == "getChat"
            || (rd.funcName == "getChatAdministrators"))
        {
            if ((rd.funcName == "getChat") && _printGetChat)
                printToLog();

            if ((rd.funcName == "getChatAdministrators") && _printGetChatAdministrators)
                printToLog();
        }
        else
            printToLog();
    });
}

void Application::httpResultHandler(const ReplyData& rd)
{
    if (rd.funcName == "getMe")
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

        QString commandPrefix;
        config::base().getValue("bot.command_prefix", commandPrefix);

        for (int i = 0; i < procCount; ++i)
        {
            tbot::Processing* p = new tbot::Processing;
            if (!p->init(_botUserId, commandPrefix))
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

            chk_connect_q(p, &tbot::Processing::resetSpam,
                          this, &Application::resetSpam);

            chk_connect_q(p, &tbot::Processing::updateBotCommands,
                          this, &Application::updateBotCommands);

            chk_connect_q(p, &tbot::Processing::updateChatAdminInfo,
                          this, &Application::updateChatAdminInfo);

            chk_connect_d(&config::observerBase(), &config::ObserverBase::changed,
                          p, &tbot::Processing::reloadConfig)

            _procList.add(p);
        }
        for (tbot::Processing* p : _procList)
            p->start();

        reloadConfig();
    }
    else if (rd.funcName == "getChat")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        qint64 chatId = rd.params["chat_id"].toLongLong();
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
                            tbot::groupChats(&chats);
                        }
                    }
                    tbot::HttpParams params;
                    params["chat_id"] = chatId;
                    sendTgCommand("getChatAdministrators", params);
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
    else if (rd.funcName == "sendMessage")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        if (!httpResult.ok)
            return;

        // Удаляем служебные сообщения бота
        tbot::SendMessage_Result result;
        if ((rd.messageDel >= 0) && result.fromJson(rd.data))
        {
            qint64 chatId = result.message.chat->id;
            qint64 userId = result.message.from->id;
            qint32 messageId = result.message.message_id;

            if (_botUserId == userId)
            {
                tbot::HttpParams params;
                params["chat_id"] = chatId;
                params["message_id"] = messageId;

                int delay = qMax(rd.messageDel*1000, 500 /*0.5 сек*/);
                sendTgCommand("deleteMessage", params, delay);
            }
        }
    }
    else if (rd.funcName == "getChatAdministrators")
    {
        ++_getChatAdminCallCount;

        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        if (!httpResult.ok)
            return;

        qint64 chatId = rd.params["chat_id"].toLongLong();
        tbot::GroupChat::List chats = tbot::groupChats();
        if (lst::FindResult fr = chats.findRef(chatId))
        {
            tbot::GetChatAdministrators_Result result;
            if (result.fromJson(rd.data))
            {
                QSet<qint64> adminIds;
                QSet<qint64> ownerIds;
                tbot::ChatMemberAdministrator::Ptr botInfo;
                for (const tbot::ChatMemberAdministrator::Ptr& item : result.items)
                    if (item->user)
                    {
                        adminIds.insert(item->user->id);
                        if (item->status == "creator")
                            ownerIds.insert(item->user->id);

                        if (item->user->id == _botUserId)
                            botInfo = item;
                    }

                tbot::GroupChat* chat = chats.item(fr.index());
                chat->setAdminIds(adminIds);
                chat->setOwnerIds(ownerIds);
                chat->setBotInfo(botInfo);
            }
        }

        if (chats.count() == _getChatAdminCallCount)
        {
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
            _getChatAdminCallCount = 0;
        }
    }
    else if (rd.funcName == "banChatMember"
             || rd.funcName == "restrictChatMember")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
            return;

        qint64 chatId = rd.params["chat_id"].toLongLong();
        qint64 userId = rd.params["user_id"].toLongLong();

        tbot::GroupChat::List chats = tbot::groupChats();
        tbot::GroupChat* chat = chats.findItem(&chatId);

        lst::FindResult fr = _spammers.findRef(qMakePair(chatId, userId));

        { //Block for alog::Line
            alog::Line logLine = httpResult.ok ? log_verbose_m : log_error_m;

            if (rd.funcName == "banChatMember")
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

            if (fr.success())
            {
                Spammer* spammer = _spammers.item(fr.index());
                logLine << spammer->user->first_name;

                logLine << "/";
                if (!spammer->user->last_name.isEmpty())
                    logLine << spammer->user->last_name;

                logLine << "/";
                if (!spammer->user->username.isEmpty())
                    logLine << "@" << spammer->user->username;
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
                if (rd.funcName == "restrictChatMember")
                {
                    uint untilDate = rd.params["until_date"].toUInt();
                    logLine << ". Restricted to " << QDateTime::fromTime_t(untilDate);
                }
            }
            else
            {
                logLine << ". Perhaps bot has not rights to block/restric users";
                return;
            }
        }
        if (fr.success() && (rd.funcName == "banChatMember"))
            _spammers.remove(fr.index());
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
        QString username;
        if (user && !user->username.isEmpty())
            username = "@" + user->username;

        if (user)
            logLine << log_format(
                ". User first/last/uname/id: %?/%?/%?/%?",
                user->first_name, user->last_name, username, user->id);

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

            tbot::HttpParams params;
            params["chat_id"] = chat->id;
            params["user_id"] = spammer->user->id;
            params["until_date"] = qint64(std::time(nullptr)) + restrictTime + 5;
            params["permissions"] = permissions;
            params["use_independent_chat_permissions"] = false;
            sendTgCommand("restrictChatMember", params, 3*1000 /*3 сек*/);
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
                    tbot::HttpParams params;
                    params["chat_id"] = chat->id;
                    params["user_id"] = spammer->user->id;
                    params["until_date"] = qint64(std::time(nullptr));
                    params["revoke_messages"] = false;
                    sendTgCommand("banChatMember", params, 3*1000 /*3 сек*/);
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
        QString username;
        if (!user->username.isEmpty())
            username = "@" + user->username;

        // Сообщение для Telegram
        QString botMsg =
            u8"Аннулированы спам-штрафы для пользователя [%1 %2](tg://user?id=%3)";
        botMsg = botMsg.arg(user->first_name).arg(user->last_name).arg(userId);

        if (!username.isEmpty())
        {
            QString s = QString(" (%1/%2)").arg(username).arg(userId);
            botMsg += s.replace("_", "\\_");
        }
        else
            botMsg += QString(" (id: %1)").arg(userId);

        tbot::HttpParams params;
        params["chat_id"] = chatId;
        params["text"] = botMsg;
        params["parse_mode"] = "Markdown";
        emit sendTgCommand("sendMessage", params, 1.5*1000 /*1.5 сек*/);

        // Сообщение в лог
        alog::Line logLine = log_verbose_m << log_format(
            "Spam penalties canceled. User first/last/uname/id: %?/%?/%?/%?",
            user->first_name, user->last_name, username, user->id);

        tbot::GroupChat::List chats = tbot::groupChats();
        if (tbot::GroupChat* chat = chats.findItem(&chatId))
            logLine << log_format(". Chat name/id: %?/%?", chat->name(), chat->id);
        else
            logLine << log_format(". Chat id: %?", chatId);
    }
}

void Application::updateBotCommands()
{
    qint64 timemark = QDateTime::currentDateTime().toMSecsSinceEpoch();
    saveBotCommands(timemark);

    if (_masterMode)
    {
        data::TimelimitSync timelimitSync;
        timelimitSync.timemark = timemark;
        timelimitSync.chats = tbot::timelimitInactiveChats().toList();

        // Отправляем событие с изменениями timelimit
        Message::Ptr m = createJsonMessage(timelimitSync, {Message::Type::Event});
        tcp::listener().send(m);
    }
}

void Application::updateChatAdminInfo(qint64 chatId)
{
    tbot::HttpParams params;
    params["chat_id"] = chatId;
    sendTgCommand("getChatAdministrators", params);
}

void Application::loadBotCommands()
{
    QList<qint64> timelimitInactiveChats;
    config::state().getValue("timelimit_inactive.chats", timelimitInactiveChats);
    tbot::setTimelimitInactiveChats(timelimitInactiveChats);
}

void Application::saveBotCommands(qint64 timemark)
{
    config::state().remove("timelimit_inactive");
    config::state().setValue("timelimit_inactive.timemark", timemark);
    config::state().setValue("timelimit_inactive.chats",
                             tbot::timelimitInactiveChats().toList());
    config::state().saveFile();
}

void Application::loadReportSpam()
{
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
        return true;
    };
    config::state().getValue("spammers", loadFunc, false);
    _spammers.sort();
}

void Application::saveReportSpam()
{
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
    config::state().remove("spammers");
    config::state().setValue("spammers", saveFunc);
    config::state().saveFile();
}
