// TODO(Audit Directive 2, Appendix C Â§2.2): promote to include/caudio/service/service.hpp â€” daemon runtime, not CLI-specific. Keep include/cli/service/service.hpp as deprecated shim for one release: #include "caudio/service/service.hpp".
#pragma once

#include "caudio/service/ipc_channel.hpp"
#include "caudio/service/ipc_server.hpp"
#include "caudio/service/service_impl.hpp"
#include "caudio/service/shm_status.hpp"
