#include "transport_test_support.h"

#include "esphome/core/application.h"

#include <algorithm>
#include <unordered_map>

namespace esphome {
namespace epaper_spectra6_133 {
namespace test_support {
namespace {

std::unordered_map<Transport *, TransportState> g_transport_states;

}  // namespace

Operation make_operation(OperationType type, const TransportState &state) {
  Operation operation;
  operation.type = type;
  operation.cs_levels = state.cs_levels;
  return operation;
}

template<typename Queue, typename Value> Value pop_or_default(Queue &queue, Value default_value) {
  if (queue.empty()) {
    return default_value;
  }

  const Value value = queue.front();
  queue.pop_front();
  return value;
}

TransportState &transport_state(Transport &transport) { return g_transport_states[&transport]; }

void reset_transport_state(Transport &transport) {
  g_transport_states[&transport] = TransportState{};
  ::esphome::App.feed_wdt_calls = 0;
}

}  // namespace test_support

bool Transport::init_gpio() { return true; }

bool Transport::init_spi() { return true; }

bool Transport::wait_busy_high(uint32_t timeout_ms) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kWaitBusyHigh, state);
  operation.timeout_ms = timeout_ms;
  state.operations.push_back(operation);
  return test_support::pop_or_default(state.wait_busy_high_results, true);
}

void Transport::hardware_reset() {
  auto &state = test_support::transport_state(*this);
  state.operations.push_back(test_support::make_operation(test_support::OperationType::kHardwareReset, state));
}

void Transport::set_cs_all(uint32_t level) {
  auto &state = test_support::transport_state(*this);
  state.cs_levels = {level, level};
  auto operation = test_support::make_operation(test_support::OperationType::kSetCsAll, state);
  operation.level = level;
  state.operations.push_back(operation);
}

void Transport::set_cs(uint8_t index, uint32_t level) {
  auto &state = test_support::transport_state(*this);
  if (index < state.cs_levels.size()) {
    state.cs_levels[index] = level;
  }
  auto operation = test_support::make_operation(test_support::OperationType::kSetCs, state);
  operation.cs_index = index;
  operation.level = level;
  state.operations.push_back(operation);
}

void Transport::set_load_switch(uint32_t level) {
  auto &state = test_support::transport_state(*this);
  state.load_switch_level = level;
  auto operation = test_support::make_operation(test_support::OperationType::kSetLoadSwitch, state);
  operation.level = level;
  state.operations.push_back(operation);
}

void Transport::delay_ms(uint32_t ms) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kDelayMs, state);
  operation.delay_ms = ms;
  state.operations.push_back(operation);
}

int Transport::busy_level() const {
  return test_support::transport_state(const_cast<Transport &>(*this)).mock_busy_level;
}

esp_err_t Transport::write_command(uint8_t command) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kWriteCommand, state);
  operation.command = command;
  state.operations.push_back(operation);
  return test_support::pop_or_default(state.write_command_results, ESP_OK);
}

esp_err_t Transport::write_data(const uint8_t *data, size_t length) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kWriteData, state);
  operation.length = length;
  if (data != nullptr && length > 0) {
    operation.data.assign(data, data + length);
  }
  state.operations.push_back(operation);
  return test_support::pop_or_default(state.write_data_results, ESP_OK);
}

esp_err_t Transport::write_register(uint8_t command, const uint8_t *data, size_t length) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kWriteRegister, state);
  operation.command = command;
  operation.length = length;
  if (data != nullptr && length > 0) {
    operation.data.assign(data, data + length);
  }
  state.operations.push_back(operation);
  return test_support::pop_or_default(state.write_register_results, ESP_OK);
}

esp_err_t Transport::read_register(uint8_t command, uint8_t *data, size_t length) {
  auto &state = test_support::transport_state(*this);
  auto operation = test_support::make_operation(test_support::OperationType::kReadRegister, state);
  operation.command = command;
  operation.length = length;
  state.operations.push_back(operation);

  const auto response = test_support::pop_or_default(state.read_register_results, test_support::ReadResponse{});
  if (data != nullptr && length > 0) {
    std::fill(data, data + length, 0);
    const size_t copy_length = std::min(length, response.data.size());
    std::copy_n(response.data.begin(), copy_length, data);
  }
  return response.err;
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
