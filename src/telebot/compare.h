#pragma once

#include "shared/list.h"
#include <QtGlobal>

namespace tbot {

template<typename T> struct CompareId
{
    int operator() (const T* item1, const T* item2) const
        {return LIST_COMPARE_ITEM(item1->id, item2->id);}

    int operator() (const qint64* id, const T* item2) const
        {return LIST_COMPARE_ITEM(*id, item2->id);}
};

template<typename T> struct CompareChat
{
    int operator() (const T* item1, const T* item2) const
    {return LIST_COMPARE_ITEM(item1->chatId, item2->chatId);}

    int operator() (const qint64* chatId, const T* item2) const
    {return LIST_COMPARE_ITEM(*chatId, item2->chatId);}
};

} // namespace tbot
