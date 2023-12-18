#pragma once

#include "tele_data.h"

#include "shared/list.h"
#include "shared/defmac.h"
#include "shared/steady_timer.h"
#include "shared/qt/qthreadex.h"

#include <QtCore>
#include <QNetworkReply>
#include <atomic>

namespace tbot {

typedef QMap<QString, QVariant> HttpParams;

class Processing : public QThreadEx
{
public:
    typedef lst::List<Processing, lst::CompareItemDummy> List;

    Processing();

    bool init(qint64 botUserId);
    static void addUpdate(const QByteArray&);

signals:
    void sendTgCommand(const QString& funcName, const tbot::HttpParams&,
                       int delay = 0, int attempt = 1, int messageDel = 0);
    void reportSpam(qint64 chatId, const tbot::User::Ptr&);
    void updateBotCommands();

public slots:
    void reloadConfig();

private:
    Q_OBJECT
    DISABLE_DEFAULT_COPY(Processing)

    void run() override;

    // Обрабатывает команды для бота
    bool botCommand(const tbot::Update&);

private:
    static QMutex _threadLock;
    static QWaitCondition _threadCond;
    static QList<QByteArray> _updates;

    volatile bool _configChanged = {true};

    struct MediaGroup
    {
        // Одно из сообщений медиа-группы плохое
        bool isBad = {false};

        // Идентификатор чата для сообщений медиа-группы
        qint64 chatId = {0};
        QSet<qint64> messageIds;

        // Таймер для очистки _mediaGroups
        steady_timer timer;

    };
    static QMap<QString /*media_group_id*/, MediaGroup> _mediaGroups;

    bool _spamIsActive;
    QString _spamMessage;

    qint64 _botUserId = {0};
};

} // namespace tbot
