#pragma once

#include "tele_data.h"
#include "processing.h"

#include "pproto/func_invoker.h"
#include "pproto/transport/tcp.h"

#include "commands/commands.h"
#include "commands/error.h"

#include <QtCore>
#include <QCoreApplication>
#include <QSslKey>
#include <QTcpSocket>
#include <QSslSocket>
#include <QTcpServer>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QNetworkAccessManager>

#include <atomic>
#include <chrono>

using namespace std;
using namespace pproto;
using namespace pproto::transport;

class SslServer : public QTcpServer
{
public:
    typedef container_ptr<SslServer> Ptr;
    SslServer() : QTcpServer(nullptr) {}

signals:
    void connectionReady();

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    Q_OBJECT
};

class Application : public QCoreApplication
{
public:
    Application(int& argc, char** argv);

    bool init();
    void deinit();

    static void stop() {_stop = true;}
    static bool isStopped() {return _stop;}

    struct ReplyData
    {
        QNetworkReply* reply = {nullptr};
        quint64 replyNumer = {0};

        // Имя TG-функции
        QString funcName;

        // Параметры http запроса
        tbot::HttpParams params;

        // Результат выполнения http запроса в raw-формате
        QByteArray data;

        // Счетчик попыток выполнения http запроса
        int attempt = {1};

        // Определяет тайм-аут для удаления сервисных сообщений бота, задается
        // в секундах. Если значение тайм-аута равно 0 сообщение будет удалено
        // немедленно, таким образом оно останется только в истории сообщений.
        // При значении параметра меньше 0 сообщение не удаляется
        int messageDel = {0};

        // Признак успешно выполненного http запроса
        bool success = {true};
    };

public slots:
    void stop(int exitCode);
    void message(const pproto::Message::Ptr&);

    void socketConnected(pproto::SocketDescriptor);
    void socketDisconnected(pproto::SocketDescriptor);

    void webhook_newConnection();
    void webhook_readyConnection();
    void webhook_readyRead();
    void webhook_disconnected();
    void webhook_socketError(QAbstractSocket::SocketError);
    void webhook_sslSocketError(const QList<QSslError>&);

    void http_readyRead();
    void http_finished();
    void http_error(QNetworkReply::NetworkError);
    void http_encrypted();
    void http_sslErrors(const QList<QSslError>&);

    void reloadConfig();
    void reloadBotMode();
    void reloadGroups();
    void startRequest();

    void timelimitCheck();

    // Функция для отправки http команды.
    //   delay - задержка отправки команды, задается в миллисекундах;
    //   attempt - номер попытки выполняемого запроса.
    //   messageDel - см. описание в структуре ReplyData
    void sendTgCommand(const QString& funcName, const tbot::HttpParams&,
                       int delay = 0, int attempt = 1, int messageDel = 0);

    // Функция-обработчик http ответов
    void httpResultHandler(const ReplyData&);

    // Обработка штрафов за спам-сообщения
    void reportSpam(qint64 chatId, const tbot::User::Ptr&);

    void updateBotCommands();
    void updateChatAdminInfo(qint64 chatId);

private:
    Q_OBJECT
    void timerEvent(QTimerEvent* event) override;

    void command_SlaveAuth(const Message::Ptr&);
    void command_ConfSync(const Message::Ptr&);
    void command_TimelimitSync(const Message::Ptr&);

    void loadReportSpam();
    void saveReportSpam();

     void loadBotCommands();
     void saveBotCommands(qint64 timemark);

private:
    static QUuidEx _applId;
    static volatile bool _stop;
    static std::atomic_int _exitCode;

    int _stopTimerId = {-1};
    int _slaveTimerId = {-1};
    int _timelimitTimerId = {-1};
    int _updateAdminsTimerId = {-1};

    QString _botId;
    qint64  _botUserId = {0};

    bool _masterMode = {true};
    bool _listenerInit = {false};

    QSslKey _sslKey;
    QSslCertificate _sslCert;
    SslServer::Ptr _webhookServer;

    tbot::Processing::List _procList;

    typedef QVector<QPair<SocketDescriptor, steady_timer>> SocketPair;
    SocketPair _waitAuthSockets;  // Список сокетов ожидающих авторизацию
    SocketPair _waitCloseSockets; // Список сокетов ожидающих закрытие

    tcp::Socket::Ptr _slaveSocket; // Сокет для соединения с master-ботом

    FunctionInvoker _funcInvoker;

    struct Spammer
    {
        qint64 chatId = {0};
        tbot::User::Ptr user;

        // Время регистрации штрафа (в секундах) и одновременно счетчик
        // количества спама
        QList<std::time_t> spamTimes;

        struct Compare
        {
            int operator() (const Spammer* item1, const Spammer* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(item1->chatId,   item2->chatId)
                LIST_COMPARE_MULTI_ITEM(item1->user->id, item2->user->id);
                return 0;
            }
            int operator() (const QPair<qint64 /*chat id*/, qint64 /*user id*/>* pair,
                            const Spammer* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(pair->first  /*chat id*/, item2->chatId)
                LIST_COMPARE_MULTI_ITEM(pair->second /*user id*/, item2->user->id);
                return 0;
            }
        };
        typedef lst::List<Spammer, Compare> List;
    };

    Spammer::List _spammers;

    struct TimeLimit
    {
        qint64  chatId = {0};
        QString triggerName;
        steady_timer timer;

        struct Compare
        {
            int operator() (const TimeLimit* item1, const TimeLimit* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(item1->chatId, item2->chatId)
                return item1->triggerName.compare(item2->triggerName);
            }
            int operator() (const QPair<qint64 /*chat id*/, QString /*trigger name*/>* pair,
                            const TimeLimit* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(pair->first, item2->chatId)
                return pair->second.compare(item2->triggerName);
            }
        };
        typedef lst::List<TimeLimit, Compare> List;
    };

    // Списки начала и окончания действия триггеров timelimit, используются
    // для публикации сообщений об начале и окончании работы триггера
    TimeLimit::List _timelimitBegins;
    TimeLimit::List _timelimitEnds;

    struct WebhookData
    {
        QSslSocket* socket = {nullptr};
        qintptr socketDescr = {-1};
        bool keepAlive = {false};
        qint64 dataSize = {-1};
        QByteArray data;
    };

    QHash<quint64 /*socket id*/, WebhookData> _webhookMap;
    QHash<quint64 /*socket id*/, ReplyData>   _httpReplyMap;

    typedef container_ptr<QNetworkAccessManager> QNetworkAccessManagerPtr;
    QNetworkAccessManagerPtr _networkAccManager;

    // Счетчик вызова ТГ-функции "getChatAdministrators"
    int _getChatAdminCallCount = {0};

    bool _printGetChat = {true};
    bool _printGetChatAdministrators = {true};
};
