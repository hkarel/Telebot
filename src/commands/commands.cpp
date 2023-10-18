#include "commands.h"
#include "pproto/commands/pool.h"

namespace pproto {
namespace command {

#define REGISTRY_COMMAND_SINGLPROC(COMMAND, UUID) \
    const QUuidEx COMMAND = command::Pool::Registry{UUID, #COMMAND, false};

#define REGISTRY_COMMAND_MULTIPROC(COMMAND, UUID) \
    const QUuidEx COMMAND = command::Pool::Registry{UUID, #COMMAND, true};

REGISTRY_COMMAND_SINGLPROC(SlaveAuth,     "465b7f57-b261-404c-b9bd-6bfda90f3fce")
REGISTRY_COMMAND_SINGLPROC(ConfSync,      "39cb6323-f283-478c-8be5-9ba4643f0c2f")
REGISTRY_COMMAND_SINGLPROC(TimelimitSync, "518bf145-3808-472c-9861-00f5368b1062")

#undef REGISTRY_COMMAND_SINGLPROC
#undef REGISTRY_COMMAND_MULTIPROC

} // namespace command
} // namespace pproto
