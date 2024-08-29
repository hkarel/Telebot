#pragma once

#include "shared/list.h"
#include <QtGlobal>
#include <QString>

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

template<typename T> struct CompareUser
{
    int operator() (const T* item1, const T* item2) const
        {return LIST_COMPARE_ITEM(item1->userId, item2->userId);}

    int operator() (const qint64* userId, const T* item2) const
        {return LIST_COMPARE_ITEM(*userId, item2->userId);}
};

template<typename T> struct CompareName
{
    int operator() (const T* item1, const T* item2) const
        {return item1->name.compare(item2->name, Qt::CaseInsensitive);}

    int operator() (const QString* name, const T* item2) const
        {return name->compare(item2->name, Qt::CaseInsensitive);}
};
