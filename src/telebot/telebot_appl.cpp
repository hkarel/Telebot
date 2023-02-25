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
    _updateAdminsTimerId = startTimer(60*60*1000 /*1 час*/);

    chk_connect_a(&config::changeChecker(), &config::ChangeChecker::changed,
                  this, &Application::configChanged)

    chk_connect_q(this, &Application::signalSendTgCommand,
                  this, &Application::sendTgCommand);

    qRegisterMetaType<ReplyData>("ReplyData");
    qRegisterMetaType<tbot::HttpParams>("tbot::HttpParams");
    qRegisterMetaType<tbot::User::Ptr>("tbot::User::Ptr");
}

bool Application::init()
{
    _botId.clear();
    if (!config::base().getValue("bot.id", _botId))
        return false;

    _webhookServer = SslServer::Ptr::create_ptr();
    _networkAccManager = QNetworkAccessManagerPtr::create_ptr();

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

    QMetaObject::invokeMethod(this, "startRequest",  Qt::QueuedConnection);

    return true;
}

void Application::deinit()
{
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
}

void Application::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == _stopTimerId)
    {
        if (_stop)
        {
            KILL_TIMER(_stopTimerId)
            KILL_TIMER(_updateAdminsTimerId)

            exit(_exitCode);
            return;
        }
    }
    else if (event->timerId() == _updateAdminsTimerId)
    {
        // Обновляем весь конфиг и список актуальных админов
        configChanged();
    }
}

void Application::stop(int exitCode)
{
    _exitCode = exitCode;
    stop();
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

        tbot::Processing::addUpdate(wd.data);

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
        log_error_m << "Input data larger than 'Content-Length'";
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

    log_debug_m << log_format("Http answer (reply id: %?) func   : %?",
                              rd.replyNumer, rd.funcName);
    { //Block for alog::Line
        alog::Line logLine =
            log_debug_m << log_format("Http answer (reply id: %?) params : ",
                                      rd.replyNumer);
        for (auto&& it = rd.params.cbegin(); it != rd.params.cend(); ++it)
            logLine << it.key() << ": " << it.value().toString() << "; ";
    }
    log_debug_m << log_format("Http answer (reply id: %?) return : %?",
                              rd.replyNumer, rd.data);

    QMetaObject::invokeMethod(this, "httpResultHandler",  Qt::QueuedConnection,
                              Q_ARG(ReplyData, rd));

    _httpReplyMap.remove(replyId);
    reply->deleteLater();
}

void Application::http_error(QNetworkReply::NetworkError /*error*/)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    quint64 replyId = reinterpret_cast<quint64>(reply);
    const ReplyData& rd = _httpReplyMap[replyId];

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

void Application::startRequest()
{
    tbot::HttpParams params;
    sendTgCommand("getMe", params);
}

void Application::configChanged()
{
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
    for (tbot::GroupChat* chat : chats)
    {
        tbot::HttpParams params;
        params["chat_id"] = chat->id;
        emit signalSendTgCommand("getChat", params);
    }
}

void Application::sendTgCommand(const QString& funcName, const tbot::HttpParams& params)
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

    log_debug_m << log_format("Http call %? (reply id: %?). Send command: %?",
                              rd.funcName, rd.replyNumer, url.toString());
}

void Application::httpResultHandler(const ReplyData& rd)
{
    if ( rd.funcName == "getMe")
    {
        tbot::HttpResult httpResult;
        if (!httpResult.fromJson(rd.data))
        {
            log_error_m << "Failed parsing response for function getMe()"
                        << ". Program will be stopped";
            Application::stop();
            return;
        }
        if (!httpResult.ok)
        {
            log_error_m << "Failed result for function getMe()"
                        << ". Program will be stopped";
            Application::stop();
            return;
        }

        tbot::GetMe_Result result;
        if (!result.fromJson(rd.data))
        {
            log_error_m << "Failed parsing data for function getMe()"
                        << ". Program will be stopped";
            Application::stop();
            return;
        }

        _botUserId = result.user.id;
        log_info_m << "--- Bot account info ---";
        log_info_m << result.user.first_name
                   << " (@" << result.user.username << ", " << _botUserId << ")";
        log_info_m << "---";

        int procCount = 1;
        config::base().getValue("bot.processing_count", procCount);

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

            chk_connect_d(&config::changeChecker(), &config::ChangeChecker::changed,
                          p, &tbot::Processing::configChanged)

            _procList.add(p);
        }
        for (tbot::Processing* p : _procList)
            p->start();

        configChanged();
    }
    else if ( rd.funcName == "getChat")
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
                    emit signalSendTgCommand("getChatAdministrators", params);
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
        if (result.fromJson(rd.data))
        {
            qint64 chatId = result.message.chat->id;
            qint64 userId = result.message.from->id;
            qint32 messageId = result.message.message_id;

            if (_botUserId == userId)
            {
                tbot::HttpParams params;
                params["chat_id"] = chatId;
                params["message_id"] = messageId;
                emit signalSendTgCommand("deleteMessage", params);
            }
        }
    }
    else if (rd.funcName == "getChatAdministrators")
    {
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
                for (const tbot::ChatMemberAdministrator& item : result.items)
                    if (item.user)
                    {
                        adminIds.insert(item.user->id);
                        if (item.status == "creator")
                            ownerIds.insert(item.user->id);
                    }

                tbot::GroupChat* chat = chats.item(fr.index());
                chat->setAdminIds(adminIds);
                chat->setOwnerIds(ownerIds);
            }
        }
    }
    else if (rd.funcName == "banChatMember")
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
            alog::Line logLine = log_error_m;

            if (!httpResult.ok)
                logLine << "Failed ban user";
            else
                logLine << "User is banned";

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

            if (!httpResult.ok)
            {
                logLine << ". Perhaps bot has not rights to block users";
                return;
            }
        }
        if (fr.success())
            _spammers.remove(fr.index());
    }
}

void Application::reportSpam(qint64 chatId, const tbot::User::Ptr& user)
{
    if (user.empty())
    {
        log_error_m << "Function reportSpam(), param 'user' is empty";
        return;
    }

    log_debug_m << log_format("Report spam 0. Id chat/spammer: %?/%?",
                              chatId, user->id);

    auto times = [](const QList<std::time_t>& spamTimes) -> QString
    {
        QString tm;
        for (std::time_t t : spamTimes)
            tm += QString::number(qint64(t)) + " ";
        return tm.trimmed();
    };

    if (lst::FindResult fr = _spammers.findRef(qMakePair(chatId, user->id)))
    {
        Spammer* spammer = _spammers.item(fr.index());
        spammer->user = user; // Переприсваиваем, потому что параметры имени
                              // могут измениться
        std::time_t curTime = std::time(nullptr);
        spammer->spamTimes.append(curTime);

        log_debug_m << log_format("Report spam 1. Id chat/spammer %?/%?. Times: [%?]",
                                  chatId, spammer->user->id, qint64(curTime));
    }
    else
    {
        Spammer* spammer = _spammers.add();
        spammer->chatId = chatId;
        spammer->user = user;
        std::time_t curTime = std::time(nullptr);
        spammer->spamTimes.append(curTime);

        _spammers.sort();

        log_debug_m << log_format("Report spam 2. Id chat/spammer %?/%?. Times: [%?]",
                                  chatId, spammer->user->id, times(spammer->spamTimes));
    }

    tbot::GroupChat::List chats = tbot::groupChats();
    for (int i = 0; i < _spammers.count(); ++i)
    {
        Spammer* spammer = _spammers.item(i);
        if (lst::FindResult fr = chats.findRef(spammer->chatId))
        {
            tbot::GroupChat* chat = chats.item(fr.index());
            if (chat->userSpamLimit <= 0)
            {
                log_debug_m << log_format("Report spam 3. Id chat/spammer: %?/%?",
                                          chat->id, spammer->user->id);
                _spammers.remove(i--);
                continue;
            }

            // Удаляем штрафы за спам если они были более суток назад,
            // предполагаем что пользователь сделал это ненамеренно и
            // за последующие сутки больше не спамил
            std::time_t curTime = std::time(nullptr);
            for (int j = 0; j < spammer->spamTimes.count(); ++j)
            {
                const int64_t elapsedTime = curTime - spammer->spamTimes.at(j);
                if (qAbs(elapsedTime) > 60*60*24 /*1 сутки*/)
                {
                    log_debug_m << log_format(
                        "Report spam 4. Id chat/spammer: %?/%?. Times: [%?]",
                        chat->id, spammer->user->id, times(spammer->spamTimes));

                    { //Block for alog::Line
                        alog::Line logLine = log_debug_m
                            << "Spam penalty has expired (1 day)"
                            << ". User first/last/uname/id: "
                            << spammer->user->first_name;

                        logLine << "/";
                        if (!spammer->user->last_name.isEmpty())
                            logLine << spammer->user->last_name;

                        logLine << "/";
                        if (!spammer->user->username.isEmpty())
                            logLine << "@" << spammer->user->username;

                        logLine << "/" << spammer->user->id;

                        logLine << ". Time: " << QString::number(spammer->spamTimes.at(j));

                        logLine << ". Chat name/id: "
                                << chat->name() << "/" << chat->id;
                    }
                    spammer->spamTimes.removeAt(j--);
                }
            }

            if (spammer->spamTimes.count() >= chat->userSpamLimit)
            {
                QSet<qint64> ownerIds = chat->ownerIds();
                if (ownerIds.contains(spammer->user->id))
                {
                    log_debug_m << log_format(
                        "Report spam 5. Id chat/spammer: %?/%?. Times: [%?]",
                        chat->id, spammer->user->id, times(spammer->spamTimes));
                    { //Block for alog::Line
                        alog::Line logLine = log_verbose_m
                            << "Owner of chat cannot be banned"
                            << ", first/last/uname/id: "
                            << spammer->user->first_name;

                        logLine << "/";
                        if (!spammer->user->last_name.isEmpty())
                            logLine << spammer->user->last_name;

                        logLine << "/";
                        if (!spammer->user->username.isEmpty())
                            logLine << "@" << spammer->user->username;

                        logLine << "/" << spammer->user->id;

                        logLine << ". Chat name/id: "
                                << chat->name() << "/" << chat->id;
                    }
                    _spammers.remove(i--);
                    continue;
                }

                log_debug_m << log_format(
                    "Report spam 6. Id chat/spammer: %?/%?. Times: [%?]",
                    chat->id, spammer->user->id, times(spammer->spamTimes));

                tbot::HttpParams params;
                params["chat_id"] = chat->id;
                params["user_id"] = spammer->user->id;
                params["until_date"] = qint64(std::time(nullptr));
                params["revoke_messages"] = false;
                emit signalSendTgCommand("banChatMember", params);
            }
        }
        else
        {
            log_debug_m << log_format(
                "Report spam 7. Id chat/spammer: %?/%?. Times: [%?]",
                chatId, spammer->user->id, times(spammer->spamTimes));
            _spammers.remove(i--);
        }
    }
}
