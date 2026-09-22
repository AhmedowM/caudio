// TODO(Audit Directive 2, Appendix C §2.2): promote to include/caudio/client/client.hpp — client SDK, not CLI-specific. Keep include/cli/client/client.hpp as deprecated shim for one release: #include "caudio/client/client.hpp".
/**
 * @file client.hpp
 * @brief caudio.client module interface: re-exports IPC client, implementation, and output
 * formatter.
 * @ingroup caudio_client
 */
#pragma once

#include "cli/cli.hpp"
#include "cli/client/client_impl.hpp"
#include "cli/client/ipc_client.hpp"
#include "cli/client/output_formatter.hpp"
#include "cli/service/service.hpp"
