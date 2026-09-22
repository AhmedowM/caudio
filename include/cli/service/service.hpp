// TODO(Audit Directive 2, Appendix C §2.2): promote to include/caudio/service/service.hpp — daemon runtime, not CLI-specific. Keep include/cli/service/service.hpp as deprecated shim for one release: #include "caudio/service/service.hpp".
#pragma once

#include "cli/service/ipc_channel.hpp"
#include "cli/service/ipc_server.hpp"
#include "cli/service/service_impl.hpp"
#include "cli/service/shm_status.hpp"
