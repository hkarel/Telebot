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

void UserJoinTimeList::add(qint64 chatId, qint64 userId, bool joinViaChatFolder)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (_list.sortState() != lst::SortState::Up)
        _list.sort();

    lst::FindResult fr = _list.findRef(tuple{chatId, userId});
    if (fr.success())
    {
        data::UserJoinTime* ujt = _list.item(fr.index());
        ujt->time = std::time(nullptr);
        ujt->joinViaChatFolder = joinViaChatFolder;
        _changeFlag = true;

        log_debug_m << log_format(
            "The re-adding to list UserJoinTimes. Chat/User/Time/JoinVCF: %?/%?/%?/%?",
            ujt->chatId, ujt->userId, ujt->time, ujt->joinViaChatFolder);
        return;
    }

    data::UserJoinTime* ujt {new data::UserJoinTime};
    ujt->add_ref();
    ujt->chatId = chatId;
    ujt->userId = userId;
    ujt->time = std::time(nullptr);
    ujt->joinViaChatFolder = joinViaChatFolder;

    _list.addInSort(ujt, fr);
    _changeFlag = true;

    log_debug_m << log_format(
        "User added to list UserJoinTimes. Chat/User/Time/JoinVCF: %?/%?/%?/%?",
        ujt->chatId, ujt->userId, ujt->time, ujt->joinViaChatFolder);
}

void UserJoinTimeList::removeByTime()
{
    QMutexLocker locker {&_mutex}; (void) locker;

    const qint64 diff = 60*24*60*60; // 60 суток
    const qint64 threshold = std::time(nullptr) - diff;
    _list.removeCond([threshold, this](data::UserJoinTime* ujt) -> bool
    {
        if (ujt->time < threshold)
        {
            log_debug_m << log_format(
                "User removed from list UserJoinTimes. Chat/User/Time/JoinVCF: %?/%?/%?/%?",
                ujt->chatId, ujt->userId, ujt->time, ujt->joinViaChatFolder);
            return (_changeFlag = true);
        }
        return false;
    });
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

bool WhiteUserList::remove(data::WhiteUser::Ptr whiteUser)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (lst::FindResult fr = _list.find(whiteUser.get()))
    {
        data::WhiteUser::Ptr wu = data::WhiteUser::Ptr(_list.item(fr.index()));
        _list.remove(fr.index());
        _changeFlag = true;

        log_debug_m << log_format(
            "User removed from list WhiteUsers. Chat/User/Info: %?/%?/%?",
            wu->chatId, wu->userId, wu->info);
        return true;
    }
    return false;
}

data::WhiteUser::List WhiteUserList::chatList(qint64 chatId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    data::WhiteUser::List list;
    for (data::WhiteUser* wu : _list)
        if (wu->chatId == chatId)
        {
            wu->add_ref();
            list.add(wu);
        }
    list.sort();
    return list;
}

WhiteUserList& whiteUsers()
{
    return safe::singleton<WhiteUserList>();
}

void SpamUserList::add(qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (_list.sortState() != lst::SortState::Up)
        _list.sort();

    lst::FindResult fr = _list.findRef(userId);
    if (fr.success())
    {
        data::SpamUser* su = _list.item(fr.index());
        su->time = std::time(nullptr);
        _changeFlag = true;

        log_debug_m << log_format(
            "The re-adding to list SpamUsers. User/Time: %?/%?",
            su->userId, su->time);
        return;
    }

    data::SpamUser* su {new data::SpamUser};
    su->add_ref();
    su->userId = userId;
    su->time = std::time(nullptr);

    _list.addInSort(su, fr);
    _changeFlag = true;

    log_debug_m << log_format(
        "User added to list SpamUsers. User/Time: %?/%?",
        su->userId, su->time);
}

bool SpamUserList::remove(qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (lst::FindResult fr = _list.findRef(userId))
    {
        _list.remove(fr.index());
        _changeFlag = true;

        log_debug_m << log_format("User %? removed from list SpamUsers", userId);
        return true;
    }
    return false;
}

void SpamUserList::removeByTime()
{
    QMutexLocker locker {&_mutex}; (void) locker;

    const qint64 diff = 60*24*60*60; // 60 суток
    const qint64 threshold = std::time(nullptr) - diff;
    _list.removeCond([threshold, this](data::SpamUser* su) -> bool
    {
        if (su->time < threshold)
        {
            log_debug_m << log_format(
                "User removed from list SpamUsers. User/Time: %?/%?",
                su->userId, su->time);
            return (_changeFlag = true);
        }
        return false;
    });
}

SpamUserList& spamUsers()
{
    return safe::singleton<SpamUserList>();
}

} //namespace tbot
