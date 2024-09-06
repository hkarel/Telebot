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

        // Параметры для функции sendTgCommand()
        tbot::TgParams::Ptr params;

        // Результат выполнения http запроса в raw-формате
        QByteArray data;

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

    void reloadGroup(qint64 chatId, bool botCommand);
    void reloadGroups(const QString& configFile);

    void startRequest();
    void timelimitCheck();

    // Функция для отправки Телеграм-команды
    void sendTgCommand(const tbot::TgParams::Ptr&);

    // Функция-обработчик http ответов
    void httpResultHandler(const ReplyData&);

    // Обработка штрафов за спам-сообщения
    void reportSpam(qint64 chatId, const tbot::User::Ptr&);

    // Аннулирование штрафов за спам-сообщения
    void resetSpam(qint64 chatId, qint64 userId);

    // Учет пользователей и сообщений в системе Anti-Raid
    void antiRaidUser(qint64 chatId, const tbot::User::Ptr&);
    void antiRaidMessage(qint64 chatId, qint64 userId, qint32 messageId);

    void restrictNewUser(qint64 chatId, qint64 userId);


private:
    Q_OBJECT
    void timerEvent(QTimerEvent* event) override;

    void command_SlaveAuth(const Message::Ptr&);
    void command_ConfSync(const Message::Ptr&);
    void command_TimelimitSync(const Message::Ptr&);
    void command_UserTriggerSync(const Message::Ptr&);

    void loadReportSpam();
    void saveReportSpam();

    void loadBotCommands();
    void saveBotCommands(const QString& section, qint64 timemark);
    void updateBotCommands(const QString& section);

    void loadAntiRaidCache();
    void saveAntiRaidCache();

    void sendToProcessing(const tbot::MessageData::Ptr&);

    // Обрабатывает команды для бота
    bool botCommand(const tbot::MessageData::Ptr&);

private:
    static QUuidEx _applId;
    static volatile bool _stop;
    static std::atomic_int _exitCode;

    int _stopTimerId = {-1};
    int _slaveTimerId = {-1};
    int _antiraidTimerId = {-1};
    int _timelimitTimerId = {-1};
    int _configStateTimerId = {-1};
    int _updateAdminsTimerId = {-1};

    QString _botId;
    qint64  _botUserId = {0};
    QString _commandPrefix;
    QString _commandPrefixShort;

    bool _masterMode = {true};
    bool _listenerInit = {false};

    QSslKey _sslKey;
    QSslCertificate _sslCert;
    SslServer::Ptr _webhookServer;

    bool _localServer = {false};
    QString _localServerAddr;
    int _localServerPort = {0};

    tbot::Processing::List _procList;

    typedef QVector<QPair<SocketDescriptor, steady_timer>> SocketPair;
    SocketPair _waitAuthSockets;   // Список сокетов ожидающих авторизацию
    SocketPair _waitCloseSockets;  // Список сокетов ожидающих закрытие

    tcp::Socket::Ptr _slaveSocket; // Сокет для соединения с master-ботом

    QDateTime _botStartTime;       // Время старта бота
    QDateTime _masterStartTime;    // Время старта master-бота, используется
                                   // в slave-режиме
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

    struct AntiRaid
    {
        qint64 chatId = {0};

        // Время окончания действия Anti-Raid режима (в UTC)
        QDateTime deactiveTime;

        // Временный список новых участников, используется для детектирования
        // Anti-Raid атаки
        tbot::User::List usersTmp;

        // Список участников вступивших в группу во время Anti-Raid атаки, все
        // пользователи из этого списка будут заблокированы
        tbot::User::List usersBan;

        // Механизм приостановки удаления следующего  пользователя,  пока
        // не будет получено подтверждение удаления текущего пользователя
        bool skipUsersBan = {false};
        steady_timer skipUsersBanTimer;

        // Идентификаторы сообщений, которые успели написать заблокированные
        // пользователи
        QList<qint32> messageIds;

        // Механизм приостановки удаления следующего  сообщения,  пока
        // не будет получено подтверждение удаления текущего сообщения
        bool skipMessageIds = {false};
        steady_timer skipMessageIdsTimer;

        // Счетчик пропуска команд блокировки пользователей
        int sleepBanCount = {0};

        // Счетчик пропуска команд удаления сообщений
        int sleepMsgCount = {0};

        typedef lst::List<AntiRaid, CompareChat<AntiRaid>> List;
    };
    AntiRaid::List _antiRaidCache;
    int _antiRaidSaveStep = {0};

    struct NewUser
    {
        qint64 userId = {0};
        QSet<qint64> chatIds;
        steady_timer timer;

        typedef lst::List<NewUser, CompareUser<NewUser>> List;
    };
    NewUser::List _newUsers;

    data::UserTrigger::List _userTriggers;

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

    bool _printTriggers = {true};
    bool _printGroupChats = {true};
    bool _printGetChat = {true};
    bool _printGetChatAdmins = {true};
};
