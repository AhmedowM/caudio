set(CAUDIO_DB_SOURCES
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
caudio_add_component(db SOURCES ${CAUDIO_DB_SOURCES} DEPS caudio::utils caudio::player Threads::Threads INCLUDES vendor)
target_include_directories(db PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(db PRIVATE caudio_sqlite blake3)
target_compile_definitions(db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_module_warnings(db)

caudio_add_shared_variant(db EXTRA_DEPS caudio::utils_shared Threads::Threads)
