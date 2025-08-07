#pragma once

#include "commands/commands.h"

namespace tbot {

using namespace std;
using namespace pproto;

template<typename T> class DataList
{
public:
    typedef typename T::Ptr  Ptr;
    typedef typename T::List List;

    List list() const
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        return _list;

//        List rlist;
//        for (auto* t : _list)
//        {
//            t->add_ref();
//            rlist.add(t);
//        }
//        rlist.sort();
//        return rlist;
    }

    void listSwap(List& list)
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        _list.swap(list);
        if (_list.sortState() != lst::SortState::Up)
            _list.sort();
        _changeFlag = true;
    }

    template<typename F> Ptr find(const F& f)
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        if (lst::FindResult fr = _list.findRef(f))
            return Ptr(_list.item(fr.index()));
        return {};
    }

    void remove(int index)
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        _list.remove(index);
        _changeFlag = false;
    }

    bool changed()
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        return _changeFlag;
    }

    void resetChangeFlag()
    {
        QMutexLocker locker {&_mutex}; (void) locker;
        _changeFlag = false;
    }

protected:
    mutable QMutex _mutex;
    List _list;
    bool _changeFlag = {false};
};

/**
  Класс для работы с датой вступления пользователей в группу
*/
class UserJoinTimeList : public DataList<data::UserJoinTime>
{
public:
    void add(qint64 chatId, qint64 userId, bool joinViaChatFolder);
    void removeByTime();
};

UserJoinTimeList& userJoinTimes();

/**
  Класс для работы с белым списком пользователей
*/
class WhiteUserList : public DataList<data::WhiteUser>
{
public:
    void add(data::WhiteUser::Ptr);
    bool remove(data::WhiteUser::Ptr);
    data::WhiteUser::List chatList(qint64 chatId);
};

WhiteUserList& whiteUsers();

/**
  Класс для работы со списком спам-пользователей
*/
class SpamUserList : public DataList<data::SpamUser>
{
public:
    void add(qint64 userId);
    bool remove(qint64 userId);
    void removeByTime();
};

SpamUserList& spamUsers();

} //namespace tbot
