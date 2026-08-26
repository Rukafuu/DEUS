#include "deus.h"

DeusExecutionOptions deus_execution_options_default(void) {
    DeusExecutionOptions options = {0};
    options.abi_version = DEUS_EXECUTION_ABI_VERSION;
    options.instruction_limit = DEUS_DEFAULT_INSTRUCTION_LIMIT;
    return options;
}
