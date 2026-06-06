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

void UserJoinTimeList::remove(qint64 chatId, qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (lst::FindResult fr = _list.findRef(tuple{chatId, userId}))
    {
        _list.remove(fr.index());
        _changeFlag = true;
    }
}

void UserJoinTimeList::removeByTime()
{
    QMutexLocker locker {&_mutex}; (void) locker;

    const qint64 diff = 180*24*60*60; // 180 суток
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

void FuzzyTextList::add(const data::FuzzyText::Ptr& fuzzyText)
{
    QMutexLocker locker {&_mutex}; (void) locker;

    if (_list.sortState() != lst::SortState::Up)
        _list.sort();

    fuzzyText->add_ref();
    _changeFlag = true;

    lst::FindResult fr = _list.find(fuzzyText.get());
    if (fr.success())
    {
        _list.replace(fr.index(), fuzzyText.get(), true);

        log_debug_m << log_format(
            "The re-adding to list FuzzyTexts. Chat/User/Msg: %?/%?/%?",
            fuzzyText->chatId, fuzzyText->user->id, fuzzyText->messageId);
    }
    else
    {
        _list.addInSort(fuzzyText.get(), fr);

        log_debug_m << log_format(
            "Text added to list FuzzyTexts. Chat/User/Msg: %?/%?/%?",
            fuzzyText->chatId, fuzzyText->user->id, fuzzyText->messageId);
    }
}

void FuzzyTextList::removeByTime()
{
    QMutexLocker locker {&_mutex}; (void) locker;

    const qint64 curTime = std::time(nullptr);
    _list.removeCond([curTime, this](data::FuzzyText* fuzzyText) -> bool
    {
        qint64 timeLife = fuzzyText->timeLife.load();
        if (timeLife < curTime)
        {
            log_debug_m << log_format(
                "Text removed from list FuzzyTexts by timeout. Chat/User/Msg: %?/%?/%?",
                fuzzyText->chatId, fuzzyText->user->id, fuzzyText->messageId);
            return (_changeFlag = true);
        }
        return false;
    });
}

data::FuzzyText::List FuzzyTextList::textSimilarity(
                                        const data::FuzzyText::Ptr& fuzzyText) const
{
    QMutexLocker locker {&_mutex}; (void) locker;

    data::FuzzyText::List list;
    const u32string text32 = fuzzyText->text.toLower().toStdU32String();
    for (data::FuzzyText* ft : _list)
    {
        if (fuzzyText->chatId == ft->chatId
            && fuzzyText->messageId == ft->messageId)
            continue;

        if (ft->fuzzyCache.empty())
        {
            const u32string t32 = ft->text.toLower().toStdU32String();
            ft->fuzzyCache = data::FuzzyText::FuzzyCachePtr::create(t32);
        }

        double res = ft->fuzzyCache->similarity(text32, 90);
        if (res > 90)
        {
            ft->add_ref();
            list.add(ft);
        }
    }
    return list;
}

FuzzyTextList& fuzzyTexts()
{
    return safe::singleton<FuzzyTextList>();
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
            "User %? re-added to list SpamUsers", su->userId);
        return;
    }

    data::SpamUser* su {new data::SpamUser};
    su->add_ref();
    su->userId = userId;
    su->time = std::time(nullptr);

    _list.addInSort(su, fr);
    _changeFlag = true;

    log_debug_m << log_format(
        "User %? added to list SpamUsers", su->userId);
}

bool SpamUserList::check(qint64 userId)
{
    QMutexLocker locker {&_mutex}; (void) locker;
    return bool(_list.findRef(userId));
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

    const qint64 diff = 90*24*60*60; // 90 суток
    const qint64 threshold = std::time(nullptr) - diff;
    _list.removeCond([threshold, this](data::SpamUser* su) -> bool
    {
        if (su->time < threshold)
        {
            log_debug_m << log_format(
                "User %? removed from list SpamUsers by timeout", su->userId);
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
