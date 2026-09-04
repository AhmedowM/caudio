#include <vector>
#include <string>
#include <string_view>
#include <typeinfo>
import caudio.utils;

namespace caudio::utils::test {

bool log_injected() {
  std::vector<std::pair<Level, std::string>> out;
  Logger logger([&](Level lvl, std::string_view msg) {
    out.emplace_back(lvl, std::string(msg));
  });
  logger.log(Level::Info, "hello {}", 42);
  if (out.size() != 1) return false;
  if (out[0].second.compare("hello 42") != 0) return false;
  if (out[0].first != Level::Info) return false;
  return true;
}

bool log_level_filter() {
  std::vector<std::pair<Level, std::string>> out;
  Logger logger([&](Level lvl, std::string_view msg) {
    out.emplace_back(lvl, std::string(msg));
  }, Level::Warn);
  logger.log(Level::Debug, "debug msg");
  logger.log(Level::Info, "info msg");
  if (!out.empty()) return false;
  logger.log(Level::Warn, "warn msg");
  if (out.size() != 1) return false;
  if (out[0].first != Level::Warn) return false;
  logger.log(Level::Error, "error msg");
  if (out.size() != 2) return false;
  return true;
}

bool log_convenience() {
  std::vector<std::pair<Level, std::string>> out;
  Logger logger([&](Level lvl, std::string_view msg) {
    out.emplace_back(lvl, std::string(msg));
  });
  logger.debug("dbg {}", 1);
  logger.info("inf {}", 2);
  logger.warn("wrn {}", 3);
  logger.error("err {}", 4);
  if (out.size() != 4) return false;
  if (out[0].first != Level::Debug) return false;
  if (out[0].second.compare("dbg 1") != 0) return false;
  if (out[1].first != Level::Info) return false;
  if (out[2].first != Level::Warn) return false;
  if (out[3].first != Level::Error) return false;
  return true;
}

bool log_set_callback_level() {
  Logger logger;
  std::vector<std::string> out;
  logger.log(Level::Info, "no cb yet");
  if (!out.empty()) return false;
  logger.setCallback([&](Level, std::string_view msg) { out.emplace_back(msg); });
  logger.log(Level::Info, "now captured");
  if (out.size() != 1) return false;
  if (out[0].compare("now captured") != 0) return false;
  logger.setLevel(Level::Error);
  logger.log(Level::Warn, "filtered");
  if (out.size() != 1) return false;
  logger.log(Level::Error, "passes");
  if (out.size() != 2) return false;
  return true;
}

bool log_null_safe() {
  Logger logger(nullptr);
  logger.log(Level::Info, "no crash");
  logger.debug("also no crash");
  return true;
}

bool log_toString_level() {
  if (toString(Level::Debug).compare("Debug") != 0) return false;
  if (toString(Level::Info).compare("Info") != 0) return false;
  if (toString(Level::Warn).compare("Warn") != 0) return false;
  if (toString(Level::Error).compare("Error") != 0) return false;
  return true;
}

} // namespace caudio::utils::test
