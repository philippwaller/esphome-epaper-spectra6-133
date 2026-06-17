#pragma once

#include <map>
#include <string>

#include "esphome/core/log.h"

namespace esphome {
namespace logger {

class Logger {
 public:
  explicit Logger(uint8_t level = ESPHOME_LOG_LEVEL_INFO) : current_level_(level) {}

  void set_log_level(uint8_t level) { this->current_level_ = level; }
  void set_log_level(const char *tag, uint8_t level) { this->log_levels_[tag] = level; }

  uint8_t level_for(const char *tag) {
    const auto it = this->log_levels_.find(tag);
    if (it != this->log_levels_.end()) {
      return it->second;
    }
    return this->current_level_;
  }

 private:
  uint8_t current_level_;
  std::map<std::string, uint8_t> log_levels_;
};

inline Logger *global_logger = nullptr;

}  // namespace logger
}  // namespace esphome
