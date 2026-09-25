// TODO(Audit Directive 2, Appendix C ├é┬º2.2): promote to include/caudio/client/client.hpp ├óΓé¼ΓÇ¥ client SDK, not CLI-specific. Keep include/cli/client/client.hpp as deprecated shim for one release: #include <caudio/client/client.hpp>.
/**
 * @file client.hpp
 * @brief caudio.client module interface: re-exports IPC client, implementation, and output
 * formatter.
 * @ingroup caudio_client
 */
#pragma once

#include <caudio/ipc.hpp>
#include <caudio/client/client_impl.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/service/service.hpp>
