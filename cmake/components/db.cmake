set(CAUDIO_DB_MODULE_SOURCES
  src/db/database.cppm
  src/db/db_types.cppm
  src/db/schema.cppm
  src/db/db_core.cppm
  src/db/queue.cppm
  src/db/scan.cppm
  src/db/search.cppm
  src/db/json.cppm
  src/db/write_thread.cppm
  src/db/statement.cppm
  src/db/transaction.cppm
  src/db/detail.cppm
  src/db/fingerprint.cppm
  src/db/fts.cppm
  src/db/stmt_helpers.cppm
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
caudio_add_component(db SOURCES ${CAUDIO_DB_SOURCES} MODULE_SOURCES ${CAUDIO_DB_MODULE_SOURCES} DEPS caudio::utils caudio::player Threads::Threads INCLUDES vendor)
target_include_directories(db PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(db PRIVATE caudio_sqlite blake3)
target_compile_definitions(db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_module_warnings(db)

caudio_add_shared_variant(db EXTRA_DEPS caudio::utils_shared Threads::Threads)
