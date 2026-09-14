/**
 * @file client.cppm
 * @brief caudio.client module interface: re-exports IPC client, implementation, and output formatter.
 * @ingroup caudio_client
 */
export module caudio.client;

export import :ipc_client;
export import :impl;
export import :output_formatter;
export import caudio.service;
export import caudio.cli;
