#pragma once

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

    bool init();
    static void addUpdate(const QByteArray&);

signals:
    void sendTgCommand(const QString& funcName, const tbot::HttpParams&);

public slots:
    void configChanged();

private:
    Q_OBJECT
    DISABLE_DEFAULT_COPY(Processing)

    void run() override;

private:
    static QMutex _threadLock;
    static QWaitCondition _threadCond;
    static QList<QByteArray> _updates;

    volatile bool _configChanged = {true};

    bool _spamIsActive;
    QString _spamMessage;

    //QString _botId;
};

} // namespace tbot
