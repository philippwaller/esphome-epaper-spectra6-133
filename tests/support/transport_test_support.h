#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "esp_err.h"
#include "epaper_spectra6_133_transport.h"

namespace esphome {
namespace epaper_spectra6_133 {
namespace test_support {

enum class OperationType {
  kHardwareReset,
  kWaitBusyHigh,
  kSetCsAll,
  kSetCs,
  kSetLoadSwitch,
  kDelayMs,
  kWriteCommand,
  kWriteData,
  kWriteRegister,
  kReadRegister,
};

struct Operation {
  OperationType type;
  std::array<uint32_t, 2> cs_levels{1, 1};
  uint8_t cs_index{0};
  uint32_t level{0};
  uint32_t timeout_ms{0};
  uint32_t delay_ms{0};
  uint8_t command{0};
  size_t length{0};
  std::vector<uint8_t> data;
};

struct ReadResponse {
  esp_err_t err{ESP_OK};
  std::vector<uint8_t> data;
};

struct TransportState {
  std::vector<Operation> operations;
  std::deque<bool> wait_busy_high_results;
  std::deque<esp_err_t> write_command_results;
  std::deque<esp_err_t> write_data_results;
  std::deque<esp_err_t> write_register_results;
  std::deque<ReadResponse> read_register_results;
  std::array<uint32_t, 2> cs_levels{1, 1};
  uint32_t load_switch_level{0};
  // Controls the value returned by Transport::busy_level().
  // 1 = not busy (BUSY pin HIGH, panel idle).
  // 0 = busy     (BUSY pin LOW, panel executing refresh).
  int mock_busy_level{1};
};

TransportState &transport_state(Transport &transport);
void reset_transport_state(Transport &transport);

}  // namespace test_support
}  // namespace epaper_spectra6_133
}  // namespace esphome
