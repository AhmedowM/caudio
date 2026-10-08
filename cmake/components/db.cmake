# db.cmake -- caudio::db (SQLite library/engine-state persistence).
# NOTE: statement/transaction/queue/schema/detail/fingerprint/fts/stmt_helpers
# live in src/db/ (NOT installed) and have no module partitions.
set(CAUDIO_DB_MODULE_SOURCES
  modules/caudio/database.cppm
  modules/caudio/db/types.cppm
  modules/caudio/db/core.cppm
  modules/caudio/db/scan.cppm
  modules/caudio/db/search.cppm
  modules/caudio/db/json.cppm
  modules/caudio/db/write_thread.cppm
)

set(CAUDIO_DB_SOURCES
  src/db/core.cpp
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
target_link_libraries(db PRIVATE caudio::sqlite3 blake3)
target_include_directories(db SYSTEM PRIVATE ${CAUDIO_NLOHMANN_PRIVATE_INCLUDE})
target_compile_definitions(db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_module_warnings(db)

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(db EXTRA_DEPS caudio::utils_shared Threads::Threads)
endif()
