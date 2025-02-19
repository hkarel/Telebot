#include "functions.h"

#include "shared/break_point.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"
#include "shared/safe_singleton.h"

#include <QtCore>
#include <tuple>

#define log_error_m   alog::logger().error  (alog_line_location, "Functions")
#define log_warn_m    alog::logger().warn   (alog_line_location, "Functions")
#define log_info_m    alog::logger().info   (alog_line_location, "Functions")
#define log_verbose_m alog::logger().verbose(alog_line_location, "Functions")
#define log_debug_m   alog::logger().debug  (alog_line_location, "Functions")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "Functions")

namespace tbot {

void UserJoinTimeList::add(qint64 chatId, qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (_list.sortState() != lst::SortState::Up)
        _list.sort();

    lst::FindResult fr = _list.findRef(tuple{chatId, userId});
    if (fr.success())
    {
        data::UserJoinTime* ujt = _list.item(fr.index());
        ujt->time = std::time(nullptr);
        _changeFlag = true;

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

    _list.addInSort(ujt, fr);
    _changeFlag = true;

    log_debug_m << log_format(
        "User added to list UserJoinTimes. Chat/User/Time: %?/%?/%?",
        ujt->chatId, ujt->userId, ujt->time);
}

void UserJoinTimeList::removeByTime()
{
    QMutexLocker locker {&_mutex}; (void) locker;

    const qint64 diffTime = 15*24*60*60; // 15 суток
    const qint64 time = std::time(nullptr) - diffTime;
    _list.removeCond([time, diffTime](data::UserJoinTime* ujt) -> bool
    {
        bool result = (ujt->time < time);
        if (result)
            log_debug_m << log_format(
                "User removed from list UserJoinTimes. Chat/User/Time: %?/%?/%?",
                ujt->chatId, ujt->userId, ujt->time);
        return result;
    });
    _changeFlag = true;
}

UserJoinTimeList& userJoinTimes()
{
    return safe::singleton<UserJoinTimeList>();
}

void WhiteUserList::add(data::WhiteUser::Ptr whiteUser)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (_list.sortState() != lst::SortState::Up)
        _list.sort();

    whiteUser->add_ref();
    _changeFlag = true;

    lst::FindResult fr = _list.find(whiteUser.get());
    if (fr.success())
    {
        _list.replace(fr.index(), whiteUser.get(), true);

        log_debug_m << log_format(
            "The re-adding to list WhiteUsers. Chat/User/Info: %?/%?/%?",
            whiteUser->chatId, whiteUser->userId, whiteUser->info);
    }
    else
    {
        _list.addInSort(whiteUser.get(), fr);

        log_debug_m << log_format(
            "User added to list WhiteUsers. Chat/User/Info: %?/%?/%?",
            whiteUser->chatId, whiteUser->userId, whiteUser->info);
    }
}

void WhiteUserList::remove(qint64 chatId, qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (lst::FindResult fr = _list.findRef(tuple{chatId, userId}))
    {
        data::WhiteUser::Ptr wu = data::WhiteUser::Ptr(_list.item(fr.index()));
        _list.remove(fr.index());
        _changeFlag = true;

        log_debug_m << log_format(
            "User removed from list WhiteUsers. Chat/User/Info: %?/%?/%?",
            wu->chatId, wu->userId, wu->info);
    }
}

WhiteUserList& whiteUsers()
{
    return safe::singleton<WhiteUserList>();
}


} //namespace tbot
