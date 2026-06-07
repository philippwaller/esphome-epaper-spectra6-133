#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "esphome/core/application.h"
#include "epaper_spectra6_133_controller.h"
#include "transport_test_support.h"

namespace esphome {
namespace epaper_spectra6_133 {
namespace {

using test_support::Operation;
using test_support::OperationType;
using test_support::ReadResponse;
using test_support::TransportState;

std::vector<Operation> filter_operations_by_type(const std::vector<Operation> &operations, OperationType type) {
  std::vector<Operation> filtered;
  for (const auto &operation : operations) {
    if (operation.type == type) {
      filtered.push_back(operation);
    }
  }
  return filtered;
}

std::vector<Operation> filter_register_writes_by_command(const std::vector<Operation> &operations, uint8_t command) {
  std::vector<Operation> filtered;
  for (const auto &operation : operations) {
    if (operation.type == OperationType::kWriteRegister && operation.command == command) {
      filtered.push_back(operation);
    }
  }
  return filtered;
}

std::vector<Operation> filter_data_writes_by_cs_levels(const std::vector<Operation> &operations,
                                                       std::array<uint32_t, 2> cs_levels) {
  std::vector<Operation> filtered;
  for (const auto &operation : operations) {
    if (operation.type == OperationType::kWriteData && operation.cs_levels == cs_levels) {
      filtered.push_back(operation);
    }
  }
  return filtered;
}

std::vector<uint8_t> write_register_commands(const std::vector<Operation> &operations) {
  std::vector<uint8_t> commands;
  for (const auto &operation : operations) {
    if (operation.type == OperationType::kWriteRegister) {
      commands.push_back(operation.command);
    }
  }
  return commands;
}

std::vector<uint32_t> wait_timeouts(const std::vector<Operation> &operations) {
  std::vector<uint32_t> timeouts;
  for (const auto &operation : operations) {
    if (operation.type == OperationType::kWaitBusyHigh) {
      timeouts.push_back(operation.timeout_ms);
    }
  }
  return timeouts;
}

std::vector<uint32_t> delays(const std::vector<Operation> &operations) {
  std::vector<uint32_t> values;
  for (const auto &operation : operations) {
    if (operation.type == OperationType::kDelayMs) {
      values.push_back(operation.delay_ms);
    }
  }
  return values;
}

std::vector<uint8_t> make_framebuffer_pattern() {
  std::vector<uint8_t> framebuffer(FULL_FRAME_SIZE);
  for (size_t index = 0; index < framebuffer.size(); index++) {
    framebuffer[index] = static_cast<uint8_t>(index & 0xFF);
  }
  return framebuffer;
}

class ControllerTest : public ::testing::Test {
 protected:
  ControllerTest() : controller_(transport_) {}

  void SetUp() override { test_support::reset_transport_state(transport_); }

  TransportState &state() { return test_support::transport_state(transport_); }

  Transport transport_;
  Controller controller_;
};

TEST_F(ControllerTest, InitPanelProgramsExpectedRegisterSequence) {
  ASSERT_TRUE(controller_.init_panel());

  EXPECT_TRUE(controller_.is_initialized());
  EXPECT_EQ(write_register_commands(state().operations),
            (std::vector<uint8_t>{AN_TM, CMD66, PSR, CDI, TCON, AGID, PWS, CCSET, TRES, PWR, EN_BUF, BTST_P,
                                  BOOST_VDDP_EN, BTST_N, BUCK_BOOST_VDDN, TFT_VCOM_POWER}));
}

TEST_F(ControllerTest, InitPanelFailsWhenBusyNeverReleasesAfterReset) {
  state().wait_busy_high_results.push_back(false);

  EXPECT_FALSE(controller_.init_panel());
  EXPECT_FALSE(controller_.is_initialized());
  EXPECT_TRUE(write_register_commands(state().operations).empty());
}

TEST_F(ControllerTest, RefreshSendsPonDrfPofSequence) {
  ASSERT_TRUE(controller_.refresh());

  const auto write_commands = filter_operations_by_type(state().operations, OperationType::kWriteCommand);
  ASSERT_EQ(write_commands.size(), 1U);
  EXPECT_EQ(write_commands[0].command, PON);

  EXPECT_EQ(write_register_commands(state().operations), (std::vector<uint8_t>{DRF, POF}));
  EXPECT_EQ(wait_timeouts(state().operations), (std::vector<uint32_t>{8000U, 20000U, 8000U}));
  EXPECT_EQ(delays(state().operations), (std::vector<uint32_t>{30U}));
}

TEST_F(ControllerTest, TransferFullFrameReturnsFalseForNullFramebuffer) {
  EXPECT_FALSE(controller_.transfer_full_frame(nullptr));
  EXPECT_TRUE(state().operations.empty());
}

TEST_F(ControllerTest, TransferRegionAlignsSingleHalfAndArmsDummyRegionOnOtherHalf) {
  const auto framebuffer = make_framebuffer_pattern();

  ASSERT_TRUE(controller_.transfer_region(framebuffer.data(), 10, 5, 6, 3));

  const auto partial_windows = filter_register_writes_by_command(state().operations, PTLW);
  ASSERT_EQ(partial_windows.size(), 3U);
  EXPECT_EQ(partial_windows[0].cs_levels, (std::array<uint32_t, 2>{0, 1}));
  EXPECT_EQ(partial_windows[1].cs_levels, (std::array<uint32_t, 2>{1, 0}));
  EXPECT_EQ(partial_windows[2].cs_levels, (std::array<uint32_t, 2>{0, 0}));

  const auto left_half_writes = filter_data_writes_by_cs_levels(state().operations, {0, 1});
  ASSERT_EQ(left_half_writes.size(), 4U);
  for (size_t row = 0; row < left_half_writes.size(); row++) {
    const size_t row_offset = static_cast<size_t>(4 + row) * ROW_BYTES + 4;
    EXPECT_EQ(left_half_writes[row].length, 8U);
    EXPECT_EQ(left_half_writes[row].data,
              std::vector<uint8_t>(framebuffer.begin() + static_cast<std::ptrdiff_t>(row_offset),
                                   framebuffer.begin() + static_cast<std::ptrdiff_t>(row_offset + 8)));
  }

  EXPECT_TRUE(filter_data_writes_by_cs_levels(state().operations, {1, 0}).empty());
  EXPECT_EQ(App.feed_wdt_calls, 1);
}

TEST_F(ControllerTest, TransferRegionReturnsFalseForRectanglesOutsidePanel) {
  const auto framebuffer = make_framebuffer_pattern();

  EXPECT_FALSE(controller_.transfer_region(framebuffer.data(), EPD_WIDTH + 10, 0, 20, 20));
  EXPECT_TRUE(filter_register_writes_by_command(state().operations, PTLW).empty());
}

TEST_F(ControllerTest, TransferRegionDisablesPartialRegionsAfterTransferFailure) {
  const auto framebuffer = make_framebuffer_pattern();
  state().write_data_results.push_back(ESP_FAIL);

  EXPECT_FALSE(controller_.transfer_region(framebuffer.data(), 10, 5, 6, 3));

  const auto partial_windows = filter_register_writes_by_command(state().operations, PTLW);
  ASSERT_FALSE(partial_windows.empty());
  EXPECT_EQ(partial_windows.back().cs_levels, (std::array<uint32_t, 2>{0, 0}));
  EXPECT_EQ(partial_windows.back().data, (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

TEST_F(ControllerTest, ProbeDriversReturnsTrueWhenBothStatusBitsAreSet) {
  state().read_register_results.push_back(ReadResponse{ESP_OK, {0x01, 0xAA, 0x55}});
  state().read_register_results.push_back(ReadResponse{ESP_OK, {0x01, 0xBB, 0x66}});

  EXPECT_TRUE(controller_.probe_drivers());
}

TEST_F(ControllerTest, ProbeDriversReturnsFalseWhenReadFailsOrStatusBitIsMissing) {
  state().read_register_results.push_back(ReadResponse{ESP_FAIL, {}});
  state().read_register_results.push_back(ReadResponse{ESP_OK, {0x00, 0x00, 0x00}});

  EXPECT_FALSE(controller_.probe_drivers());
}

// ---------------------------------------------------------------------------
// Step-primitive tests
// ---------------------------------------------------------------------------

// begin_half_transfer(0) sets CS0 low, then sends DTM; CS0 stays low.
TEST_F(ControllerTest, BeginHalfTransferIc0SendsDtmToIc0AndLeavesCs0Low) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.begin_half_transfer(0));

  const auto &ops = state().operations;
  // First op: set_cs(0, 0) — CS0 low.
  ASSERT_GE(ops.size(), 2U);
  EXPECT_EQ(ops[0].type, OperationType::kSetCs);
  EXPECT_EQ(ops[0].cs_index, 0U);
  EXPECT_EQ(ops[0].level, 0U);
  // Second op: write_command(DTM).
  EXPECT_EQ(ops[1].type, OperationType::kWriteCommand);
  EXPECT_EQ(ops[1].command, DTM);
  // CS0 should remain low after the call.
  EXPECT_EQ(state().cs_levels[0], 0U);
  EXPECT_EQ(state().cs_levels[1], 1U);

  controller_.end_half_transfer();  // clean up
}

// begin_half_transfer(1) sets CS1 low, then sends DTM; CS1 stays low.
TEST_F(ControllerTest, BeginHalfTransferIc1SendsDtmToIc1AndLeavesCs1Low) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.begin_half_transfer(1));

  const auto &ops = state().operations;
  ASSERT_GE(ops.size(), 2U);
  EXPECT_EQ(ops[0].type, OperationType::kSetCs);
  EXPECT_EQ(ops[0].cs_index, 1U);
  EXPECT_EQ(ops[0].level, 0U);
  EXPECT_EQ(ops[1].type, OperationType::kWriteCommand);
  EXPECT_EQ(ops[1].command, DTM);
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 0U);

  controller_.end_half_transfer();
}

TEST_F(ControllerTest, BeginHalfTransferReturnsFalseForInvalidHalfIndex) {
  EXPECT_FALSE(controller_.begin_half_transfer(2));
  EXPECT_TRUE(state().operations.empty());
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// write_half_row() sends exactly HALF_ROW_BYTES of the correct slice of the framebuffer.
TEST_F(ControllerTest, WriteHalfRowSendsHalfRowBytesAtCorrectOffset) {
  const auto framebuffer = make_framebuffer_pattern();
  ASSERT_TRUE(controller_.init_panel());
  ASSERT_TRUE(controller_.begin_half_transfer(0));
  test_support::reset_transport_state(transport_);

  const int test_row = 3;
  ASSERT_TRUE(controller_.write_half_row(framebuffer.data(), test_row, 0));

  const auto data_ops = filter_operations_by_type(state().operations, OperationType::kWriteData);
  ASSERT_EQ(data_ops.size(), 1U);
  EXPECT_EQ(data_ops[0].length, static_cast<size_t>(HALF_ROW_BYTES));

  const size_t expected_offset = static_cast<size_t>(test_row) * ROW_BYTES;  // half=0 → offset 0
  EXPECT_EQ(data_ops[0].data,
            std::vector<uint8_t>(framebuffer.begin() + static_cast<std::ptrdiff_t>(expected_offset),
                                 framebuffer.begin() + static_cast<std::ptrdiff_t>(expected_offset + HALF_ROW_BYTES)));

  controller_.end_half_transfer();
}

TEST_F(ControllerTest, WriteHalfRowReturnsFalseForInvalidInputs) {
  const auto framebuffer = make_framebuffer_pattern();

  EXPECT_FALSE(controller_.write_half_row(nullptr, 0, 0));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  EXPECT_FALSE(controller_.write_half_row(framebuffer.data(), -1, 0));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  EXPECT_FALSE(controller_.write_half_row(framebuffer.data(), EPD_HEIGHT, 0));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  EXPECT_FALSE(controller_.write_half_row(framebuffer.data(), 0, 2));
  EXPECT_TRUE(state().operations.empty());
}

// end_half_transfer() raises both CS pins.
TEST_F(ControllerTest, EndHalfTransferReleasesAllCsPins) {
  ASSERT_TRUE(controller_.init_panel());
  ASSERT_TRUE(controller_.begin_half_transfer(0));
  ASSERT_EQ(state().cs_levels[0], 0U);

  controller_.end_half_transfer();

  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// begin_region_transfer() sends CMD66, PTLW, then DTM; the IC's CS stays low.
TEST_F(ControllerTest, BeginRegionTransferSendsCmd66PtlwThenDtmAndLeavesCs0Low) {
  const auto framebuffer = make_framebuffer_pattern();
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  PartialRegion regions[2]{};
  bool has_region[2]{};
  controller_.compute_partial_regions(0, 0, 100, 100, regions, has_region);
  ASSERT_TRUE(has_region[0]);

  ASSERT_TRUE(controller_.begin_region_transfer(regions[0]));

  // expect CMD66, PTLW, set_cs(cs_index, 0), DTM in the operation sequence.
  const auto reg_cmds = write_register_commands(state().operations);
  ASSERT_GE(reg_cmds.size(), 2U);
  EXPECT_EQ(reg_cmds[0], CMD66);
  EXPECT_EQ(reg_cmds[1], PTLW);

  const auto cmd_ops = filter_operations_by_type(state().operations, OperationType::kWriteCommand);
  ASSERT_EQ(cmd_ops.size(), 1U);
  EXPECT_EQ(cmd_ops[0].command, DTM);

  // CS for IC0 remains low.
  EXPECT_EQ(state().cs_levels[regions[0].cs_index], 0U);

  controller_.end_region_transfer(regions[0].cs_index);
}

TEST_F(ControllerTest, BeginRegionTransferReturnsFalseForInvalidCsIndex) {
  PartialRegion regions[2]{};
  bool has_region[2]{};
  controller_.compute_partial_regions(0, 0, 100, 100, regions, has_region);
  ASSERT_TRUE(has_region[0]);
  regions[0].cs_index = 2;

  EXPECT_FALSE(controller_.begin_region_transfer(regions[0]));
  EXPECT_TRUE(state().operations.empty());
}

TEST_F(ControllerTest, WriteRegionRowReturnsFalseForInvalidInputs) {
  const auto framebuffer = make_framebuffer_pattern();
  PartialRegion regions[2]{};
  bool has_region[2]{};
  controller_.compute_partial_regions(0, 0, 100, 100, regions, has_region);
  ASSERT_TRUE(has_region[0]);

  EXPECT_FALSE(controller_.write_region_row(nullptr, regions[0], 0));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  EXPECT_FALSE(controller_.write_region_row(framebuffer.data(), regions[0], -1));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  EXPECT_FALSE(controller_.write_region_row(framebuffer.data(), regions[0], regions[0].height));
  EXPECT_TRUE(state().operations.empty());

  test_support::reset_transport_state(transport_);
  regions[0].cs_index = 2;
  EXPECT_FALSE(controller_.write_region_row(framebuffer.data(), regions[0], 0));
  EXPECT_TRUE(state().operations.empty());
}

// arm_dummy_region() sends CMD66+PTLW with no subsequent DTM.
TEST_F(ControllerTest, ArmDummyRegionSendsCmd66PtlwWithoutDtm) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.arm_dummy_region(1));

  const auto reg_cmds = write_register_commands(state().operations);
  ASSERT_GE(reg_cmds.size(), 2U);
  EXPECT_EQ(reg_cmds[0], CMD66);
  EXPECT_EQ(reg_cmds[1], PTLW);

  // No DTM write_command.
  const auto cmd_ops = filter_operations_by_type(state().operations, OperationType::kWriteCommand);
  EXPECT_TRUE(cmd_ops.empty());

  // CS is fully released after the call (atomic operation).
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

TEST_F(ControllerTest, ArmDummyRegionReturnsFalseForInvalidCsIndex) {
  EXPECT_FALSE(controller_.arm_dummy_region(2));
  EXPECT_TRUE(state().operations.empty());
}

// send_refresh_pon() emits the PON write_command.
TEST_F(ControllerTest, SendRefreshPonEmitsPonCommand) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.send_refresh_pon());

  const auto cmd_ops = filter_operations_by_type(state().operations, OperationType::kWriteCommand);
  ASSERT_EQ(cmd_ops.size(), 1U);
  EXPECT_EQ(cmd_ops[0].command, PON);
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// send_refresh_drf() emits a DRF write_register.
TEST_F(ControllerTest, SendRefreshDrfEmitsDrfRegisterWrite) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.send_refresh_drf());

  const auto reg_cmds = write_register_commands(state().operations);
  ASSERT_EQ(reg_cmds.size(), 1U);
  EXPECT_EQ(reg_cmds[0], DRF);
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// send_refresh_pof() emits a POF write_register.
TEST_F(ControllerTest, SendRefreshPofEmitsPofRegisterWrite) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.send_refresh_pof());

  const auto reg_cmds = write_register_commands(state().operations);
  ASSERT_EQ(reg_cmds.size(), 1U);
  EXPECT_EQ(reg_cmds[0], POF);
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// send_deep_sleep() emits DSLP with the datasheet-required 0xA5 payload.
TEST_F(ControllerTest, SendDeepSleepEmitsDslpRegisterWrite) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);

  ASSERT_TRUE(controller_.send_deep_sleep());

  const auto sleep_writes = filter_register_writes_by_command(state().operations, DSLP);
  ASSERT_EQ(sleep_writes.size(), 1U);
  EXPECT_EQ(sleep_writes[0].data, (std::vector<uint8_t>{0xA5}));
  EXPECT_EQ(sleep_writes[0].cs_levels, (std::array<uint32_t, 2>{0, 0}));
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

TEST_F(ControllerTest, SendDeepSleepReturnsFalseAndReleasesCsOnWriteFailure) {
  ASSERT_TRUE(controller_.init_panel());
  test_support::reset_transport_state(transport_);
  state().write_register_results.push_back(ESP_FAIL);

  EXPECT_FALSE(controller_.send_deep_sleep());
  EXPECT_EQ(state().cs_levels[0], 1U);
  EXPECT_EQ(state().cs_levels[1], 1U);
}

// is_display_busy() returns false when the stub reports busy_level() == 1.
TEST_F(ControllerTest, IsDisplayBusyReturnsFalseWhenBusyPinIsHigh) {
  // The transport stub always returns busy_level() == 1 (BUSY pin high = idle).
  EXPECT_FALSE(controller_.is_display_busy());
}

}  // namespace
}  // namespace epaper_spectra6_133
}  // namespace esphome
