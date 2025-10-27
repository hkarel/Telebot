#include "commands.h"
#include "pproto/commands/pool.h"

namespace pproto {
namespace command {

#define REGISTRY_COMMAND_SINGLPROC(COMMAND, UUID) \
    const QUuidEx COMMAND = command::Pool::Registry{UUID, #COMMAND, false};

#define REGISTRY_COMMAND_MULTIPROC(COMMAND, UUID) \
    const QUuidEx COMMAND = command::Pool::Registry{UUID, #COMMAND, true};

REGISTRY_COMMAND_SINGLPROC(SlaveAuth,        "465b7f57-b261-404c-b9bd-6bfda90f3fce")
REGISTRY_COMMAND_SINGLPROC(ConfSync,         "39cb6323-f283-478c-8be5-9ba4643f0c2f")
REGISTRY_COMMAND_SINGLPROC(TimelimitSync,    "518bf145-3808-472c-9861-00f5368b1062")
REGISTRY_COMMAND_SINGLPROC(UserTriggerSync,  "a18364ae-aa4f-40bd-a18b-49fe1f0bdc40")
REGISTRY_COMMAND_SINGLPROC(DeleteDelaySync,  "ba7b9f5d-57e9-43d8-af67-a45a0935ce10")
REGISTRY_COMMAND_SINGLPROC(UserJoinTimeSync, "0f9b45b3-52ce-4f12-88c7-ef85dbc7cdaf")
REGISTRY_COMMAND_SINGLPROC(WhiteUserSync,    "6d2e1789-c920-4872-b2ed-c4f780a1c6b9")
REGISTRY_COMMAND_SINGLPROC(SpamUserSync,     "b43e1702-8ceb-49a3-b85d-7c0aeeea1dd7")
REGISTRY_COMMAND_SINGLPROC(AntiRaidUsersBan, "d8789fc2-101f-475e-b8e0-0909f14f3b4c")

#undef REGISTRY_COMMAND_SINGLPROC
#undef REGISTRY_COMMAND_MULTIPROC

} // namespace command
} // namespace pproto
