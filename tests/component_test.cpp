#include <cstddef>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "esp_timer.h"  // provides g_mock_timer_us
#include "esphome/core/application.h"
#include "epaper_spectra6_133.h"
#include "transport_test_support.h"

/**
 * @file component_test.cpp
 * @brief Host-test suite for the cooperative display operation state machine.
 *
 * These tests verify the cooperative display operation API (schedule, cancel, processing state,
 * replacement semantics) without requiring real SPI hardware.  The Transport
 * layer is replaced by the existing stub that records operations and returns
 * configurable results.
 *
 * The DisplayBuffer base class is provided by the minimal stub in
 * tests/support/esphome/components/display/display_buffer.h.
 *
 * The test class must be in namespace esphome::epaper_spectra6_133 so that
 * the friend declaration inside EpaperSpectra6133 grants it private access.
 */

namespace esphome {
namespace epaper_spectra6_133 {

TEST(Version, IsNonEmptyAndContainsDot) {
  EXPECT_GT(std::strlen(VERSION), 0);
  EXPECT_NE(std::string(VERSION).find('.'), std::string::npos);
}

namespace {

size_t count_register_writes(const std::vector<test_support::Operation> &operations, uint8_t command) {
  size_t count = 0;
  for (const auto &operation : operations) {
    if (operation.type == test_support::OperationType::kWriteRegister && operation.command == command) {
      count++;
    }
  }
  return count;
}

size_t first_operation_index(const std::vector<test_support::Operation> &operations,
                             test_support::OperationType type, uint8_t command = 0) {
  for (size_t index = 0; index < operations.size(); index++) {
    const auto &operation = operations[index];
    if (operation.type != type) {
      continue;
    }
    if (type == test_support::OperationType::kWriteRegister && operation.command != command) {
      continue;
    }
    return index;
  }
  return operations.size();
}

}  // namespace

// ---------------------------------------------------------------------------
// Test fixture
//
// Named EpaperSpectra6133ComponentTest to match the friend declaration in the
// component header, granting it access to private members for state inspection.
//
// TEST_F generates subclasses of this fixture; subclasses do NOT inherit
// friend access.  Therefore all private member accesses are wrapped in
// protected helper methods defined here.  The test bodies call those methods.
// ---------------------------------------------------------------------------

class EpaperSpectra6133ComponentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_support::reset_transport_state(display_.transport_);
    esphome::App.feed_wdt_calls = 0;
    g_mock_timer_us = 0;

    // Pre-initialise the controller so ensure_initialized_() returns true
    // without going through hardware init. This lets update(), refresh(),
    // refresh_region(), and sleep() schedule operations without GPIO/SPI setup.
    display_.controller_.init_panel();

    // Allocate the framebuffer (same path as setup() but without GPIO/SPI).
    display_.init_internal_(FULL_FRAME_SIZE);
  }

  // Drain the cooperative operation loop until the operation completes or the step limit is exceeded.
  // g_mock_timer_us is advanced by 1 s each step so that all delay stages
  // (30 ms post-PON, 10/300 ms post-refresh) expire on the very next call
  // after their start time is recorded.
  void run_loop_until_done(int max_steps = 50000) {
    for (int step = 0; step < max_steps && display_.is_processing(); step++) {
      g_mock_timer_us += 1000000LL;  // +1 s
      display_.loop();
    }
  }

  // --- Accessors wrapping private member access (allowed via friend) ---
  DisplayOperationType operation_type() const { return display_.active_operation_.type; }
  DisplayOperationStage operation_stage() const { return display_.active_operation_.stage; }
  bool operation_cancelled() const { return display_.active_operation_.state == DisplayOperationState::CANCELLING; }
  int operation_region_x() const { return display_.active_operation_.region_x; }
  int operation_region_y() const { return display_.active_operation_.region_y; }
  int operation_region_width() const { return display_.active_operation_.region_width; }
  int operation_region_height() const { return display_.active_operation_.region_height; }
  bool pending_has_pending() const { return display_.pending_operation_.has_pending; }
  DisplayOperationType pending_type() const { return display_.pending_operation_.type; }
  bool is_in_hw_refresh_stage() const { return display_.is_in_hw_refresh_stage_(); }
  bool buffer_is_filled_with(uint8_t color_code) const {
    const uint8_t packed = static_cast<uint8_t>((color_code << 4) | color_code);
    if (display_.buffer_ == nullptr) {
      return false;
    }
    for (size_t i = 0; i < FULL_FRAME_SIZE; i++) {
      if (display_.buffer_[i] != packed) {
        return false;
      }
    }
    return true;
  }
  test_support::TransportState &transport_state() {
    return test_support::transport_state(display_.transport_);
  }
  void reset_transport_state() { test_support::reset_transport_state(display_.transport_); }

  // Simulate a busy display (BUSY pin LOW = panel executing refresh).
  void set_display_busy(bool busy) { transport_state().mock_busy_level = busy ? 0 : 1; }

  EpaperSpectra6133 display_;
};

// ---------------------------------------------------------------------------
// 1. update() schedules a display operation and sets is_processing() to true.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateSchedulesOperation) {
  EXPECT_FALSE(display_.is_processing());
  display_.update();
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::UPDATE);
}

// ---------------------------------------------------------------------------
// 2. Completing the scheduled operation resets is_processing() to false.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, OperationCompletionClearsProcessing) {
  display_.update();
  EXPECT_TRUE(display_.is_processing());

  run_loop_until_done();
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 3. Starting update_region() while update() is active supersedes the first operation.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateRegionReplacesActiveUpdate) {
  display_.update();
  EXPECT_TRUE(display_.is_processing());

  // Start a region operation — should cancel running full update.
  display_.update_region(0, 0, 100, 100);

  // Still processing (new operation is active).
  EXPECT_TRUE(display_.is_processing());

  // The new operation type is UPDATE_REGION.
  EXPECT_EQ(operation_type(), DisplayOperationType::UPDATE_REGION);
  EXPECT_EQ(operation_stage(), DisplayOperationStage::INIT);
}

// ---------------------------------------------------------------------------
// 4. refresh() while any other operation is active supersedes it.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, RefreshReplacesAnyActiveOperation) {
  display_.update();
  display_.refresh();
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::REFRESH);
}

// ---------------------------------------------------------------------------
// 5. Calling cancel() marks the operation as CANCELLING; loop() then tears it down.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelClearsProcessing) {
  display_.update();
  EXPECT_TRUE(display_.is_processing());

  display_.cancel();
  // The CANCELLING flag is set; processing remains true until loop() runs.
  EXPECT_TRUE(operation_cancelled());

  // One loop() call runs abort and clears the operation.
  display_.loop();
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 6. Calling cancel() when no operation is active is a safe no-op.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelWhenIdleIsSafe) {
  EXPECT_FALSE(display_.is_processing());
  EXPECT_NO_FATAL_FAILURE(display_.cancel());
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 7. After cancel(), further loop() calls clean up once and then exit cleanly.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelledOperationDoesNotProgressAfterTeardown) {
  display_.update();
  display_.cancel();

  // Let loop() process the cancellation.
  display_.loop();
  EXPECT_FALSE(display_.is_processing());

  // Additional loop() calls should be no-ops.
  EXPECT_NO_FATAL_FAILURE(display_.loop());
  EXPECT_NO_FATAL_FAILURE(display_.loop());
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 8. After operation completion, further loop() calls return immediately.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, LoopIsInertAfterOperationCompletion) {
  display_.refresh();
  run_loop_until_done();
  EXPECT_FALSE(display_.is_processing());

  const size_t ops_at_end = transport_state().operations.size();

  // Further loop() calls must not generate any new operations.
  display_.loop();
  display_.loop();
  display_.loop();
  EXPECT_EQ(transport_state().operations.size(), ops_at_end);
}

// ---------------------------------------------------------------------------
// Extra: refresh_region() schedules a REFRESH_REGION operation.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, RefreshRegionSchedulesCorrectOperationType) {
  display_.refresh_region(10, 20, 50, 50);
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::REFRESH_REGION);
  EXPECT_EQ(operation_region_x(), 10);
  EXPECT_EQ(operation_region_y(), 20);
  EXPECT_EQ(operation_region_width(), 50);
  EXPECT_EQ(operation_region_height(), 50);
}

// ---------------------------------------------------------------------------
// Extra: update() while another operation is active supersedes the previous operation.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateSupersedessActiveOperation) {
  display_.refresh();
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::REFRESH);

  // update() should supersede refresh(), not skip.
  EXPECT_NO_FATAL_FAILURE(display_.update());
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::UPDATE);
}

// ---------------------------------------------------------------------------
// 11. cancel() while in a refresh stage does NOT abort while BUSY is
//     asserted; once BUSY clears, the next loop() call aborts cleanly.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelDuringRefreshStageWaitsForBusy) {
  display_.refresh();

  // Advance the operation until it reaches POWER_ON (all data already transferred).
  // With mock_busy_level=1 the stub is "not busy" so WAIT_* stages pass
  // instantly.  We pump until we enter a refresh stage.
  // Simulate busy panel from POWER_ON onward.
  set_display_busy(true);
  // Run one loop step at a time until we are in a refresh stage.
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  // We should now be in a hardware-refresh stage with BUSY asserted.
  EXPECT_TRUE(is_in_hw_refresh_stage());

  display_.cancel();
  EXPECT_TRUE(operation_cancelled());

  // loop() must not abort while BUSY is asserted.
  display_.loop();
  EXPECT_TRUE(display_.is_processing());  // still busy — waiting for BUSY pin

  // Once the panel releases BUSY, the next loop() aborts the operation.
  set_display_busy(false);
  display_.loop();
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 12. Scheduling a new operation while an existing operation is in a refresh stage
//     queues it as pending and keeps is_processing() true.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, ScheduleDuringRefreshStageQueuesPendingOperation) {
  display_.refresh();

  // Advance until in a refresh stage with BUSY asserted.
  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  EXPECT_TRUE(is_in_hw_refresh_stage());

  // Schedule a replacement operation.
  display_.refresh_region(0, 0, 100, 200);

  // The old operation must be marked CANCELLING (not immediately aborted).
  EXPECT_TRUE(operation_cancelled());
  // A pending operation must have been stored.
  EXPECT_TRUE(pending_has_pending());
  EXPECT_EQ(pending_type(), DisplayOperationType::REFRESH_REGION);
  // is_processing() must still be true (pending operation counts as processing).
  EXPECT_TRUE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 13. After a pending operation is queued, it auto-starts once BUSY clears and
//     the draining operation is torn down.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, PendingOperationStartsAfterRefreshDrains) {
  display_.refresh();

  // Advance until in a refresh stage with BUSY asserted.
  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  EXPECT_TRUE(is_in_hw_refresh_stage());

  // Queue the replacement operation.
  display_.refresh_region(10, 20, 300, 400);
  EXPECT_TRUE(pending_has_pending());

  // Release BUSY; the next loop() should drain the old operation AND start pending.
  set_display_busy(false);
  display_.loop();

  // The pending operation should now be the active operation.
  EXPECT_FALSE(pending_has_pending());
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::REFRESH_REGION);
  EXPECT_EQ(operation_stage(), DisplayOperationStage::INIT);
  EXPECT_EQ(operation_region_x(), 10);
  EXPECT_EQ(operation_region_y(), 20);
  EXPECT_EQ(operation_region_width(), 300);
  EXPECT_EQ(operation_region_height(), 400);
}

// ---------------------------------------------------------------------------
// 14. cancel() while a pending operation exists discards the pending operation.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelDiscardsPendingOperation) {
  display_.refresh();

  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  display_.refresh_region(0, 0, 100, 100);  // queues as pending
  EXPECT_TRUE(pending_has_pending());

  // Explicit cancel clears both the active operation AND the pending operation.
  display_.cancel();
  EXPECT_FALSE(pending_has_pending());

  // After BUSY clears, the abort runs and nothing new starts.
  set_display_busy(false);
  display_.loop();
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 15. update_region() schedules an UPDATE_REGION operation with correct coordinates.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateRegionSchedulesOperationWithCoordinates) {
  display_.update_region(50, 100, 200, 300);
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::UPDATE_REGION);
  EXPECT_EQ(operation_region_x(), 50);
  EXPECT_EQ(operation_region_y(), 100);
  EXPECT_EQ(operation_region_width(), 200);
  EXPECT_EQ(operation_region_height(), 300);
}

// ---------------------------------------------------------------------------
// 16. is_processing() returns false only when fully idle (no active, no pending).
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, IsProcessingReturnsFalseWhenIdle) {
  EXPECT_FALSE(display_.is_processing());
  display_.refresh();
  EXPECT_TRUE(display_.is_processing());
  run_loop_until_done();
  EXPECT_FALSE(display_.is_processing());
}

// ---------------------------------------------------------------------------
// 17. clear() fills the framebuffer without scheduling a refresh.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, ClearFillsBufferWithoutRefresh) {
  // Write a non-white sentinel into the buffer before calling clear().
  if (display_.buffer_ != nullptr) {
    std::memset(display_.buffer_, 0x00, FULL_FRAME_SIZE);  // all black
  }

  display_.clear();

  EXPECT_FALSE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::NONE);
  EXPECT_TRUE(buffer_is_filled_with(COLOR_WHITE));
}

// Dispatch through the base DisplayBuffer API to verify the display's white
// default clear colour is preserved by the override.
TEST_F(EpaperSpectra6133ComponentTest, BaseClassClearDispatchUsesWhiteWithoutRefresh) {
  if (display_.buffer_ != nullptr) {
    std::memset(display_.buffer_, 0x00, FULL_FRAME_SIZE);  // all black
  }

  // Cast to base class to simulate how ESPHome's internal loop calls clear()
  esphome::display::DisplayBuffer *base_display = &display_;
  base_display->clear();

  EXPECT_FALSE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::NONE);
  EXPECT_TRUE(buffer_is_filled_with(COLOR_WHITE));
}

TEST_F(EpaperSpectra6133ComponentTest, ClearUsesConfiguredClearColor) {
  if (display_.buffer_ != nullptr) {
    std::memset(display_.buffer_, 0x11, FULL_FRAME_SIZE);  // all white
  }

  display_.set_clear_color(EpaperSpectra6133::BLUE);
  display_.clear();

  EXPECT_FALSE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::NONE);
  EXPECT_TRUE(buffer_is_filled_with(COLOR_BLUE));
}

TEST_F(EpaperSpectra6133ComponentTest, SleepSchedulesOperationAndMarksDisplaySleeping) {
  reset_transport_state();

  display_.sleep();
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::SLEEP);

  run_loop_until_done();

  EXPECT_FALSE(display_.is_processing());
  EXPECT_TRUE(display_.is_sleeping());
  EXPECT_FALSE(display_.is_ready());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 1U);
}

TEST_F(EpaperSpectra6133ComponentTest, SleepIsIdempotentWhenAlreadySleeping) {
  display_.sleep();
  run_loop_until_done();
  ASSERT_TRUE(display_.is_sleeping());
  ASSERT_FALSE(display_.is_processing());

  reset_transport_state();

  display_.sleep();

  EXPECT_FALSE(display_.is_processing());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 0U);
}

TEST_F(EpaperSpectra6133ComponentTest, UpdateAutoWakesSleepingDisplayBeforeSchedulingOperation) {
  display_.sleep();
  run_loop_until_done();
  ASSERT_TRUE(display_.is_sleeping());

  reset_transport_state();

  display_.update();

  EXPECT_FALSE(display_.is_sleeping());
  EXPECT_TRUE(display_.is_ready());
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::UPDATE);
  EXPECT_GT(count_register_writes(transport_state().operations, TRES), 0U);
}

TEST_F(EpaperSpectra6133ComponentTest, SleepDuringRefreshStageWaitsForBusyBeforeStarting) {
  display_.refresh();

  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  ASSERT_TRUE(is_in_hw_refresh_stage());

  display_.sleep();

  EXPECT_TRUE(operation_cancelled());
  EXPECT_TRUE(pending_has_pending());
  EXPECT_EQ(pending_type(), DisplayOperationType::SLEEP);

  display_.loop();
  EXPECT_TRUE(display_.is_processing());

  set_display_busy(false);
  display_.loop();
  EXPECT_TRUE(display_.is_processing());
  EXPECT_EQ(operation_type(), DisplayOperationType::SLEEP);

  run_loop_until_done();
  EXPECT_TRUE(display_.is_sleeping());
}

TEST_F(EpaperSpectra6133ComponentTest, AutoSleepSleepsAfterFullRefresh) {
  display_.set_auto_sleep(true);
  reset_transport_state();

  display_.refresh();
  run_loop_until_done();

  EXPECT_TRUE(display_.is_sleeping());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 1U);
}

TEST_F(EpaperSpectra6133ComponentTest, AutoSleepSleepsAfterPartialRefresh) {
  display_.set_auto_sleep(true);
  reset_transport_state();

  display_.refresh_region(10, 20, 80, 60);
  run_loop_until_done();

  EXPECT_TRUE(display_.is_sleeping());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 1U);
}

TEST_F(EpaperSpectra6133ComponentTest, PowerOffAfterSleepDisablesLoadSwitchAfterDslp) {
  display_.set_power_off_after_sleep(true);
  reset_transport_state();

  display_.sleep();
  run_loop_until_done();

  const auto &operations = transport_state().operations;
  const size_t dslp_index = first_operation_index(operations, test_support::OperationType::kWriteRegister, DSLP);
  const size_t load_switch_index = first_operation_index(operations, test_support::OperationType::kSetLoadSwitch);

  ASSERT_LT(dslp_index, operations.size());
  ASSERT_LT(load_switch_index, operations.size());
  EXPECT_LT(dslp_index, load_switch_index);
  EXPECT_EQ(operations[load_switch_index].level, 0U);
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
