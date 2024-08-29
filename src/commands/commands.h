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

} // namespace command

//---------------- Структуры данных используемые в сообщениях ----------------

namespace data {

struct ConfSync : Data<&command::ConfSync,
                        Message::Type::Answer,
                        Message::Type::Event>
{
    // Конфигурация групп master-бота (config-файл)
    QString config;

    J_SERIALIZE_ONE( config )
};

struct TimelimitSync : Data<&command::TimelimitSync,
                             Message::Type::Command,
                             Message::Type::Answer,
                             Message::Type::Event>
{
    qint64 timemark = {0};
    QList<qint64> chats;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( timemark )
        J_SERIALIZE_ITEM( chats    )
    J_SERIALIZE_END
};

struct UserTrigger
{
    qint64 chatId = {0};

    struct Item
    {
        QString name; // Наименование пользовательского триггера
        QString text; // Текст триггера

        typedef lst::List<Item, CompareName<Item>> List;

        J_SERIALIZE_BEGIN
            J_SERIALIZE_ITEM( name )
            J_SERIALIZE_ITEM( text )
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
        J_SERIALIZE_ITEM( chatId )
        J_SERIALIZE_ITEM( items  )
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
        J_SERIALIZE_ITEM( timemark )
        J_SERIALIZE_ITEM( triggers )
    J_SERIALIZE_END
};

} // namespace data
} // namespace pproto
