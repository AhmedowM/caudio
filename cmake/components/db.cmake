set(CAUDIO_DB_MODULE_SOURCES
  modules/db/database.cppm
  modules/db/db_types.cppm
  modules/db/schema.cppm
  modules/db/db_core.cppm
  modules/db/queue.cppm
  modules/db/scan.cppm
  modules/db/search.cppm
  modules/db/json.cppm
  modules/db/write_thread.cppm
  modules/db/statement.cppm
  modules/db/transaction.cppm
  modules/db/detail.cppm
  modules/db/fingerprint.cppm
  modules/db/fts.cppm
  modules/db/stmt_helpers.cppm
)

set(CAUDIO_DB_SOURCES
  src/db/db_core.cpp
  src/db/queue.cpp
  src/db/scan.cpp
  src/db/search.cpp
  src/db/json.cpp
  src/db/write_thread.cpp
  src/db/statement.cpp
  src/db/transaction.cpp
  src/db/fingerprint.cpp
  src/db/fts.cpp
  src/db/stmt_helpers.cpp
)
caudio_add_component(db SOURCES ${CAUDIO_DB_SOURCES} MODULE_SOURCES ${CAUDIO_DB_MODULE_SOURCES} DEPS caudio::utils caudio::player nlohmann_json::nlohmann_json Threads::Threads INCLUDES vendor)
target_link_libraries(db PRIVATE caudio_sqlite blake3)
target_compile_definitions(db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_module_warnings(db)

caudio_add_shared_variant(db EXTRA_DEPS caudio::utils_shared Threads::Threads)
