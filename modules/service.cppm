export module caudio.service;

// NOTE: :paths/:status/:audio partitions deleted (they wrapped
// src/service/*_*.hpp internals, which are NOT installed and can never
// rebuild downstream). service::detail stays header-only in src/.
export import :ipc_channel;
export import :ipc_server;
export import :shm_status;
export import :core;
