/*****************************************************************************
  В модуле представлен список идентификаторов команд для коммуникации между
  клиентской и серверной частями приложения.
  В данном модуле представлен список команд персональный для этого приложения.

  Требование надежности коммуникаций: однажды назначенный идентификатор коман-
  ды не должен более меняться.
*****************************************************************************/

#pragma once

#include "pproto/commands/base.h"
#include "compare.h"
#include "tele_data.h"

namespace pproto {
namespace command {

//----------------------------- Список команд --------------------------------

/**
  Команда авторизации для slave-бота
*/
extern const QUuidEx SlaveAuth;

/**
  Cинхронизация конфигурации для slave-бота
*/
extern const QUuidEx ConfSync;

/**
  Cинхронизация списка групп для триггеров timelimit
*/
extern const QUuidEx TimelimitSync;

/**
  Cинхронизация списка пользовательских триггеров
*/
extern const QUuidEx UserTriggerSync;

/**
  Синхронизация списка отложенного удаления сообщений
*/
extern const QUuidEx DeleteDelaySync;

/**
  Синхронизация списка с датой вступления пользователей в группу
*/
extern const QUuidEx UserJoinTimeSync;

/**
  Синхронизация списка белых пользователей
*/
extern const QUuidEx WhiteUserSync;

} // namespace command

//---------------- Структуры данных используемые в сообщениях ----------------

namespace data {

struct ConfSync : Data<&command::ConfSync,
                        Message::Type::Answer,
                        Message::Type::Event>
{
    // Конфигурация групп master-бота (config-файл)
    QString config;

    J_SERIALIZE_MAP_ONE( "config", config )
};

struct TimelimitSync : Data<&command::TimelimitSync,
                             Message::Type::Command,
                             Message::Type::Answer,
                             Message::Type::Event>
{
    qint64 timemark = {0};
    QSet<qint64> chats;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "timemark", timemark )
        J_SERIALIZE_MAP_ITEM( "chats"   , chats    )
    J_SERIALIZE_END
};

struct UserTrigger
{
    qint64 chatId = {0};

    struct Item
    {
        QList<QString> keys; // Наименования/ключи пользовательского триггера
        QString text; // Текст триггера

        struct Compare
        {
            int operator() (const Item* item1, const Item* item2) const {
                if (item1->keys.isEmpty() || item2->keys.isEmpty())
                    return -1;
                return item1->keys[0].compare(item2->keys[0], Qt::CaseInsensitive);
            }
            int operator() (const QString* key, const Item* item2) const {
                QStringList keys {item2->keys};
                return (keys.contains(*key, Qt::CaseInsensitive)) ? 0 : -1;
            }
        };
        typedef lst::List<Item, Compare> List;

        J_SERIALIZE_BEGIN
            J_SERIALIZE_MAP_ITEM( "keys", keys )
            J_SERIALIZE_MAP_ITEM( "text", text )
        J_SERIALIZE_END
    };
    Item::List items;

    UserTrigger() = default;
    UserTrigger& operator= (UserTrigger&&) = default;

    UserTrigger(const UserTrigger& ut)
    {
        chatId = ut.chatId;
        items.assign(ut.items);
    }

    typedef lst::List<UserTrigger, CompareChat<UserTrigger>> List;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "chat_id", chatId )
        J_SERIALIZE_MAP_ITEM( "items"  , items  )
    J_SERIALIZE_END
};

struct UserTriggerSync : Data<&command::UserTriggerSync,
                               Message::Type::Command,
                               Message::Type::Answer,
                               Message::Type::Event>
{
    qint64 timemark = {0};
    UserTrigger::List triggers;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "timemark", timemark )
        J_SERIALIZE_MAP_ITEM( "triggers", triggers )
    J_SERIALIZE_END
};

struct DeleteDelay
{
    qint64 chatId = {0};
    qint32 messageId = {0};
    qint64 deleteTime = {0}; // Время UTC в миллисекундах

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "chat_id"    , chatId     )
        J_SERIALIZE_MAP_ITEM( "message_id" , messageId  )
        J_SERIALIZE_MAP_ITEM( "delete_time", deleteTime )
    J_SERIALIZE_END
};

struct DeleteDelaySync : Data<&command::DeleteDelaySync,
                               Message::Type::Command,
                               Message::Type::Answer,
                               Message::Type::Event>
{
    qint64 timemark = {0};
    QList<DeleteDelay> items;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "timemark", timemark )
        J_SERIALIZE_MAP_ITEM( "items"   , items    )
    J_SERIALIZE_END
};

struct UserJoinTime : clife_base
{
    typedef clife_ptr<UserJoinTime> Ptr;

    qint64 chatId = {0};
    qint64 userId = {0};
    qint64 time   = {0}; // Время вступления пользователя в группу (в секундах)

    typedef lst::List<UserJoinTime, CompareChatUser<UserJoinTime>, clife_alloc<UserJoinTime>> List;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "c", chatId )
        J_SERIALIZE_MAP_ITEM( "u", userId )
        J_SERIALIZE_MAP_ITEM( "t", time   )
    J_SERIALIZE_END
};

// Вспомогательная структура для сериализации списка UserJoinTime
struct UserJoinTimeSerialize
{
    UserJoinTime::List items;
    J_SERIALIZE_MAP_ONE( "items", items )
};

struct UserJoinTimeSync : Data<&command::UserJoinTimeSync,
                                Message::Type::Command,
                                Message::Type::Answer,
                                Message::Type::Event>
{
    qint64 timemark = {0};
    UserJoinTime::List items;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "timemark", timemark )
        J_SERIALIZE_MAP_ITEM( "items"   , items    )
    J_SERIALIZE_END
};

struct WhiteUser : clife_base
{
    typedef clife_ptr<WhiteUser> Ptr;

    qint64  chatId = {0};
    qint64  userId = {0};

    // Краткая информация о пользователе
    QString info;

    // Администратор добавивший пользователя в белый список
    tbot::User::Ptr admin;

    // Время когда пользователь был добавлен в белый список (в миллисекундах)
    qint64 time = {0};

    typedef lst::List<WhiteUser, CompareChatUser<WhiteUser>, clife_alloc<WhiteUser>> List;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "chat_id", chatId )
        J_SERIALIZE_MAP_ITEM( "user_id", userId )
        J_SERIALIZE_MAP_ITEM( "info"   , info   )
        J_SERIALIZE_MAP_ITEM( "admin"  , admin  )
        J_SERIALIZE_MAP_ITEM( "time"   , time   )
    J_SERIALIZE_END
};

// Вспомогательная структура для сериализации списка UserJoinTime
struct WhiteUserSerialize
{
    WhiteUser::List items;
    J_SERIALIZE_MAP_ONE( "items", items )
};

struct WhiteUserSync : Data<&command::WhiteUserSync,
                             Message::Type::Command,
                             Message::Type::Answer,
                             Message::Type::Event>
{
    qint64 timemark = {0};
    WhiteUser::List items;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_MAP_ITEM( "timemark", timemark )
        J_SERIALIZE_MAP_ITEM( "items"   , items    )
    J_SERIALIZE_END
};

} // namespace data
} // namespace pproto
