#pragma once

#include "pproto/commands/base.h"

namespace pproto {
namespace error {

//--- 10 Ошибки общего плана ---
DECL_ERROR_CODE(config_not_exists,  10, "691591ba-724a-45fc-b6d6-e893e2379d90", u8"Groups config file of not exists")
DECL_ERROR_CODE(config_not_read,    10, "602e5559-9212-4e73-8f4e-d8f55b2b1320", u8"Failed open config file to read data")

} // namespace error
} // namespace pproto
