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
        List rlist;
        for (auto* t : _list)
        {
            t->add_ref();
            rlist.add(t);
        }
        rlist.sort();
        return rlist;
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
    void add(qint64 chatId, qint64 userId);
    void removeByTime();
};

UserJoinTimeList& userJoinTimes();

} //namespace tbot
