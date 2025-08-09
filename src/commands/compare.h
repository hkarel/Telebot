#pragma once

#include "shared/list.h"
#include <QtGlobal>
#include <QString>
#include <tuple>

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

template<typename T> struct CompareChatUser
{
    int operator() (const T* item1, const T* item2) const
    {
        LIST_COMPARE_MULTI_ITEM(item1->chatId, item2->chatId)
        LIST_COMPARE_MULTI_ITEM(item1->userId, item2->userId);
        return 0;
    }
    int operator() (const std::tuple<qint64 /*chat id*/, qint64 /*user id*/>* item1,
                    const T* item2) const
    {
        LIST_COMPARE_MULTI_ITEM(std::get<0>(*item1), item2->chatId)
        LIST_COMPARE_MULTI_ITEM(std::get<1>(*item1), item2->userId)
        return 0;
    }
};

template<typename T> struct CompareChatMsg
{
    int operator() (const T* item1, const T* item2) const
    {
        LIST_COMPARE_MULTI_ITEM(item1->chatId, item2->chatId)
        LIST_COMPARE_MULTI_ITEM(item1->messageId, item2->messageId);
        return 0;
    }
    int operator() (const std::tuple<qint64 /*chat id*/, qint32 /*message id*/>* item1,
                    const T* item2) const
    {
        LIST_COMPARE_MULTI_ITEM(std::get<0>(*item1), item2->chatId)
        LIST_COMPARE_MULTI_ITEM(std::get<1>(*item1), item2->messageId)
        return 0;
    }
};

template<typename T> struct CompareName
{
    int operator() (const T* item1, const T* item2) const
        {return item1->name.compare(item2->name, Qt::CaseInsensitive);}

    int operator() (const QString* name, const T* item2) const
        {return name->compare(item2->name, Qt::CaseInsensitive);}
};
