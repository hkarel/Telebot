/*****************************************************************************
  В модуле представлен список идентификаторов команд для коммуникации между
  клиентской и серверной частями приложения.
  В данном модуле представлен список команд персональный для этого приложения.

  Требование надежности коммуникаций: однажды назначенный идентификатор коман-
  ды не должен более меняться.
*****************************************************************************/

#pragma once

#include "pproto/commands/base.h"

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

} // namespace data
} // namespace pproto
