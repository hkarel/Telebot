#pragma once

#include "tele_data.h"
#include "processing.h"

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

//    void sendSettings();

    static void stop() {_stop = true;}
    static bool isStopped() {return _stop;}

    struct ReplyData
    {
        QNetworkReply* reply = {nullptr};
        quint64 replyNumer  = {0};
        QString funcName;
        tbot::HttpParams params;
        QByteArray data;
    };

signals:
    void signalSendTgCommand(const QString& funcName, const tbot::HttpParams&);

public slots:
    void stop(int exitCode);

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

    void startRequest();
    void configChanged();

    // Функция для отправки http команды
    void sendTgCommand(const QString& funcName, const tbot::HttpParams&);

    // Функция-обработчик http ответов
    void httpResultHandler(const ReplyData&);

    void reportSpam(qint64 chatId, const tbot::User::Ptr&);

private:
    Q_OBJECT
    void timerEvent(QTimerEvent* event) override;

private:
    static QUuidEx _applId;
    static volatile bool _stop;
    static std::atomic_int _exitCode;

    int _stopTimerId = {-1};
    int _updateAdminsTimerId = {-1};

    QString _botId;
    qint64  _botUserId = {0};

    QSslKey _sslKey;
    QSslCertificate _sslCert;
    SslServer::Ptr _webhookServer;

    tbot::Processing::List _procList;

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
                LIST_COMPARE_MULTI_ITEM(item1->chatId,  item2->chatId)
                LIST_COMPARE_MULTI_ITEM(item2->user->id, item1->user->id);
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
};
