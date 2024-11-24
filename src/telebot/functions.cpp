#include "functions.h"

#include "shared/break_point.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"

#include <QtCore>
#include <tuple>

#define log_error_m   alog::logger().error  (alog_line_location, "Functions")
#define log_warn_m    alog::logger().warn   (alog_line_location, "Functions")
#define log_info_m    alog::logger().info   (alog_line_location, "Functions")
#define log_verbose_m alog::logger().verbose(alog_line_location, "Functions")
#define log_debug_m   alog::logger().debug  (alog_line_location, "Functions")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "Functions")

namespace tbot {

static QMutex userJoinTimesMutex;
static data::UserJoinTime::List userJoinTimesList;
static bool userJoinTimesChangeFlag = {false};

data::UserJoinTime::List userJoinTimes()
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;

    data::UserJoinTime::List retlist;
    for (data::UserJoinTime* t : userJoinTimesList)
    {
        t->add_ref();
        retlist.add(t);
    }
    retlist.sort();
    return retlist;
}

void setUserJoinTimes(data::UserJoinTime::List& list)
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;

    userJoinTimesList.swap(list);
    userJoinTimesChangeFlag = false;

    if (userJoinTimesList.sortState() != lst::SortState::Up)
        userJoinTimesList.sort();
}

void userJoinTimesAdd(qint64 chatId, qint64 userId)
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;

    if (userJoinTimesList.sortState() != lst::SortState::Up)
        userJoinTimesList.sort();

    lst::FindResult fr = userJoinTimesList.findRef(tuple{chatId, userId});
    if (fr.success())
    {
        data::UserJoinTime* ujt = userJoinTimesList.item(fr.index());
        ujt->time = std::time(nullptr);
        userJoinTimesChangeFlag = true;

        log_debug_m << log_format(
            "The re-adding to list UserJoinTimes. Chat/User/Time: %?/%?/%?",
            ujt->chatId, ujt->userId, ujt->time);
        return;
    }

    data::UserJoinTime* ujt {new data::UserJoinTime};
    ujt->add_ref();
    ujt->chatId = chatId;
    ujt->userId = userId;
    ujt->time = std::time(nullptr);

    userJoinTimesList.addInSort(ujt, fr);
    userJoinTimesChangeFlag = true;

    log_debug_m << log_format(
        "The adding to list UserJoinTimes. Chat/User/Time: %?/%?/%?",
        ujt->chatId, ujt->userId, ujt->time);
}

void userJoinTimesRemoveByTime()
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;

    const qint64 diffTime = 15*24*60*60; // 15 суток
    const qint64 time = std::time(nullptr) - diffTime;
    userJoinTimesList.removeCond([time, diffTime](data::UserJoinTime* ujt) -> bool
    {
        bool result = (ujt->time < time);
        if (result)
            log_debug_m << log_format(
                "The removing from list UserJoinTimes. Chat/User/Time: %?/%?/%?",
                ujt->chatId, ujt->userId, ujt->time);
        return result;
    });
    userJoinTimesChangeFlag = true;
}

data::UserJoinTime::Ptr userJoinTimesFind(qint64 chatId, qint64 userId)
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;

    if (lst::FindResult fr = userJoinTimesList.findRef(tuple{chatId, userId}))
        return data::UserJoinTime::Ptr(userJoinTimesList.item(fr.index()));
    return {};
}

bool userJoinTimesChanged()
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;
    return userJoinTimesChangeFlag;
}

void userJoinTimesResetChangeFlag()
{
    QMutexLocker locker {&userJoinTimesMutex}; (void) locker;
    userJoinTimesChangeFlag = false;
}

} //namespace tbot
