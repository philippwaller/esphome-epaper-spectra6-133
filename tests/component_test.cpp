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
 * @brief Host-test suite for the async display operation state machine.
 *
 * These tests verify the cooperative async API (schedule, cancel, busy state,
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
    // without going through hardware init.  This lets update_async() and
    // friends schedule jobs without blocking on GPIO/SPI setup.
    display_.controller_.init_panel();

    // Allocate the framebuffer (same path as setup() but without GPIO/SPI).
    display_.init_internal_(FULL_FRAME_SIZE);
  }

  // Drain the async loop until the job completes or the step limit is exceeded.
  // g_mock_timer_us is advanced by 1 s each step so that all delay stages
  // (30 ms post-PON, 10/300 ms post-refresh) expire on the very next call
  // after their start time is recorded.
  void run_loop_until_done(int max_steps = 50000) {
    for (int step = 0; step < max_steps && display_.is_busy(); step++) {
      g_mock_timer_us += 1000000LL;  // +1 s
      display_.loop();
    }
  }

  // --- Accessors wrapping private member access (allowed via friend) ---
  AsyncJobType job_type() const { return display_.async_job_.type; }
  AsyncStage job_stage() const { return display_.async_job_.stage; }
  bool job_cancelled() const { return display_.async_job_.state == JobState::CANCELLING; }
  int job_region_x() const { return display_.async_job_.region_x; }
  int job_region_y() const { return display_.async_job_.region_y; }
  int job_region_width() const { return display_.async_job_.region_width; }
  int job_region_height() const { return display_.async_job_.region_height; }
  bool pending_has_pending() const { return display_.pending_job_.has_pending; }
  AsyncJobType pending_type() const { return display_.pending_job_.type; }
  bool is_in_hw_refresh_stage() const { return display_.is_in_hw_refresh_stage_(); }
  test_support::TransportState &transport_state() {
    return test_support::transport_state(display_.transport_);
  }
  void reset_transport_state() { test_support::reset_transport_state(display_.transport_); }

  // Simulate a busy display (BUSY pin LOW = panel executing refresh).
  void set_display_busy(bool busy) { transport_state().mock_busy_level = busy ? 0 : 1; }

  EpaperSpectra6133 display_;
};

// ---------------------------------------------------------------------------
// 1. update() schedules a display job and sets is_busy() to true.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateSchedulesJob) {
  EXPECT_FALSE(display_.is_busy());
  display_.update();
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::UPDATE);
}

// ---------------------------------------------------------------------------
// 2. Completing the scheduled job resets is_busy() to false.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, JobCompletionClearsBusy) {
  display_.update();
  EXPECT_TRUE(display_.is_busy());

  run_loop_until_done();
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 3. Starting update_region() while update() is active supersedes the first job.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateRegionReplacesActiveUpdate) {
  display_.update();
  EXPECT_TRUE(display_.is_busy());

  // Start a region job — should cancel the running full update.
  display_.update_region(0, 0, 100, 100);

  // Still busy (new job is active).
  EXPECT_TRUE(display_.is_busy());

  // The new job type is UPDATE_REGION.
  EXPECT_EQ(job_type(), AsyncJobType::UPDATE_REGION);
  EXPECT_EQ(job_stage(), AsyncStage::INIT);
}

// ---------------------------------------------------------------------------
// 4. flush() while any other job is active supersedes it.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, FlushReplacesAnyActiveJob) {
  display_.update();
  display_.flush();
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH);
}

// ---------------------------------------------------------------------------
// 5. Calling cancel() marks the job as CANCELLING; loop() then tears it down.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelClearsBusy) {
  display_.update();
  EXPECT_TRUE(display_.is_busy());

  display_.cancel();
  // The CANCELLING flag is set; busy is still true until loop() runs.
  EXPECT_TRUE(job_cancelled());

  // One loop() call runs abort and clears the job.
  display_.loop();
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 6. Calling cancel() when no job is active is a safe no-op.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelWhenIdleIsSafe) {
  EXPECT_FALSE(display_.is_busy());
  EXPECT_NO_FATAL_FAILURE(display_.cancel());
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 7. After cancel(), further loop() calls clean up once and then exit cleanly.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelledJobDoesNotProgressAfterTeardown) {
  display_.update();
  display_.cancel();

  // Let loop() process the cancellation.
  display_.loop();
  EXPECT_FALSE(display_.is_busy());

  // Additional loop() calls should be no-ops.
  EXPECT_NO_FATAL_FAILURE(display_.loop());
  EXPECT_NO_FATAL_FAILURE(display_.loop());
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 8. After job completion, further loop() calls return immediately.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, LoopIsInertAfterJobCompletion) {
  display_.flush();
  run_loop_until_done();
  EXPECT_FALSE(display_.is_busy());

  const size_t ops_at_end = transport_state().operations.size();

  // Further loop() calls must not generate any new operations.
  display_.loop();
  display_.loop();
  display_.loop();
  EXPECT_EQ(transport_state().operations.size(), ops_at_end);
}

// ---------------------------------------------------------------------------
// Extra: flush_region() schedules a FLUSH_REGION job.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, FlushRegionSchedulesCorrectJobType) {
  display_.flush_region(10, 20, 50, 50);
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH_REGION);
  EXPECT_EQ(job_region_x(), 10);
  EXPECT_EQ(job_region_y(), 20);
  EXPECT_EQ(job_region_width(), 50);
  EXPECT_EQ(job_region_height(), 50);
}

// ---------------------------------------------------------------------------
// Extra: update() while another job is active supersedes the previous job.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateSupersedessActiveJob) {
  display_.flush();
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH);

  // update() should supersede flush(), not skip.
  EXPECT_NO_FATAL_FAILURE(display_.update());
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::UPDATE);
}

// ---------------------------------------------------------------------------
// 11. cancel() while in a refresh stage does NOT abort while BUSY is
//     asserted; once BUSY clears, the next loop() call aborts cleanly.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelDuringRefreshStageWaitsForBusy) {
  display_.flush();

  // Advance the job until it reaches RF_PON (all data already transferred).
  // With mock_busy_level=1 the stub is "not busy" so RF_WAIT_* stages pass
  // instantly.  We pump until we enter a refresh stage.
  // Simulate busy panel from RF_PON onward.
  set_display_busy(true);
  // Run one loop step at a time until we are in a refresh stage.
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  // We should now be in a hardware-refresh stage with BUSY asserted.
  EXPECT_TRUE(is_in_hw_refresh_stage());

  display_.cancel();
  EXPECT_TRUE(job_cancelled());

  // loop() must not abort while BUSY is asserted.
  display_.loop();
  EXPECT_TRUE(display_.is_busy());  // still busy — waiting for BUSY pin

  // Once the panel releases BUSY, the next loop() aborts the job.
  set_display_busy(false);
  display_.loop();
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 12. Scheduling a new job while an existing job is in a refresh stage
//     queues it as pending and keeps is_busy() true.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, ScheduleDuringRefreshStageQueuesPendingJob) {
  display_.flush();

  // Advance until in a refresh stage with BUSY asserted.
  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  EXPECT_TRUE(is_in_hw_refresh_stage());

  // Schedule a replacement job.
  display_.flush_region(0, 0, 100, 200);

  // The old job must be marked CANCELLING (not immediately aborted).
  EXPECT_TRUE(job_cancelled());
  // A pending job must have been stored.
  EXPECT_TRUE(pending_has_pending());
  EXPECT_EQ(pending_type(), AsyncJobType::FLUSH_REGION);
  // is_busy() must still be true (pending job counts as busy).
  EXPECT_TRUE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 13. After a pending job is queued, it auto-starts once BUSY clears and
//     the draining job is torn down.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, PendingJobStartsAfterRefreshDrains) {
  display_.flush();

  // Advance until in a refresh stage with BUSY asserted.
  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  EXPECT_TRUE(is_in_hw_refresh_stage());

  // Queue the replacement job.
  display_.flush_region(10, 20, 300, 400);
  EXPECT_TRUE(pending_has_pending());

  // Release BUSY; the next loop() should drain the old job AND start pending.
  set_display_busy(false);
  display_.loop();

  // The pending job should now be the active job.
  EXPECT_FALSE(pending_has_pending());
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH_REGION);
  EXPECT_EQ(job_stage(), AsyncStage::INIT);
  EXPECT_EQ(job_region_x(), 10);
  EXPECT_EQ(job_region_y(), 20);
  EXPECT_EQ(job_region_width(), 300);
  EXPECT_EQ(job_region_height(), 400);
}

// ---------------------------------------------------------------------------
// 14. cancel() while a pending job exists discards the pending job.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, CancelDiscardsPendingJob) {
  display_.flush();

  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  display_.flush_region(0, 0, 100, 100);  // queues as pending
  EXPECT_TRUE(pending_has_pending());

  // Explicit cancel clears both the active job AND the pending job.
  display_.cancel();
  EXPECT_FALSE(pending_has_pending());

  // After BUSY clears, the abort runs and nothing new starts.
  set_display_busy(false);
  display_.loop();
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 15. update_region() schedules an UPDATE_REGION job with correct coordinates.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, UpdateRegionSchedulesJobWithCoordinates) {
  display_.update_region(50, 100, 200, 300);
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::UPDATE_REGION);
  EXPECT_EQ(job_region_x(), 50);
  EXPECT_EQ(job_region_y(), 100);
  EXPECT_EQ(job_region_width(), 200);
  EXPECT_EQ(job_region_height(), 300);
}

// ---------------------------------------------------------------------------
// 16. is_busy() returns false only when fully idle (no active, no pending).
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, IsBusyReturnsFalseWhenIdle) {
  EXPECT_FALSE(display_.is_busy());
  display_.flush();
  EXPECT_TRUE(display_.is_busy());
  run_loop_until_done();
  EXPECT_FALSE(display_.is_busy());
}

// ---------------------------------------------------------------------------
// 17. clear() fills the framebuffer and schedules a FLUSH job.
// ---------------------------------------------------------------------------
TEST_F(EpaperSpectra6133ComponentTest, ClearFillsBufferAndSchedulesFlush) {
  // Write a non-white sentinel into the buffer before calling clear().
  if (display_.buffer_ != nullptr) {
    std::memset(display_.buffer_, 0x00, FULL_FRAME_SIZE);  // all black
  }

  display_.clear();

  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH);

  // Buffer should now be filled with the white nibble (0x11 = two white pixels).
  if (display_.buffer_ != nullptr) {
    bool all_white = true;
    for (size_t i = 0; i < FULL_FRAME_SIZE; i++) {
      if (display_.buffer_[i] != 0x11) {
        all_white = false;
        break;
      }
    }
    EXPECT_TRUE(all_white);
  }
}

// Dispatch through the base DisplayBuffer API to verify the display's white
// default clear colour is preserved by the override.
TEST_F(EpaperSpectra6133ComponentTest, BaseClassClearDispatchUsesWhite) {
  if (display_.buffer_ != nullptr) {
    std::memset(display_.buffer_, 0x00, FULL_FRAME_SIZE);  // all black
  }

  // Cast to base class to simulate how ESPHome's internal loop calls clear()
  esphome::display::DisplayBuffer *base_display = &display_;
  base_display->clear();

  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::FLUSH);

  if (display_.buffer_ != nullptr) {
    bool all_white = true;
    for (size_t i = 0; i < FULL_FRAME_SIZE; i++) {
      if (display_.buffer_[i] != 0x11) {
        all_white = false;
        break;
      }
    }
    EXPECT_TRUE(all_white);
  }
}

TEST_F(EpaperSpectra6133ComponentTest, SleepSchedulesJobAndMarksDisplaySleeping) {
  reset_transport_state();

  display_.sleep();
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::SLEEP);

  run_loop_until_done();

  EXPECT_FALSE(display_.is_busy());
  EXPECT_TRUE(display_.is_sleeping());
  EXPECT_FALSE(display_.is_ready());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 1U);
}

TEST_F(EpaperSpectra6133ComponentTest, SleepIsIdempotentWhenAlreadySleeping) {
  display_.sleep();
  run_loop_until_done();
  ASSERT_TRUE(display_.is_sleeping());
  ASSERT_FALSE(display_.is_busy());

  reset_transport_state();

  display_.sleep();

  EXPECT_FALSE(display_.is_busy());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 0U);
}

TEST_F(EpaperSpectra6133ComponentTest, UpdateAutoWakesSleepingDisplayBeforeSchedulingJob) {
  display_.sleep();
  run_loop_until_done();
  ASSERT_TRUE(display_.is_sleeping());

  reset_transport_state();

  display_.update();

  EXPECT_FALSE(display_.is_sleeping());
  EXPECT_TRUE(display_.is_ready());
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::UPDATE);
  EXPECT_GT(count_register_writes(transport_state().operations, TRES), 0U);
}

TEST_F(EpaperSpectra6133ComponentTest, SleepDuringRefreshStageWaitsForBusyBeforeStarting) {
  display_.flush();

  set_display_busy(true);
  for (int i = 0; i < 2000 && !is_in_hw_refresh_stage(); i++) {
    g_mock_timer_us += 1000000LL;
    display_.loop();
  }
  ASSERT_TRUE(is_in_hw_refresh_stage());

  display_.sleep();

  EXPECT_TRUE(job_cancelled());
  EXPECT_TRUE(pending_has_pending());
  EXPECT_EQ(pending_type(), AsyncJobType::SLEEP);

  display_.loop();
  EXPECT_TRUE(display_.is_busy());

  set_display_busy(false);
  display_.loop();
  EXPECT_TRUE(display_.is_busy());
  EXPECT_EQ(job_type(), AsyncJobType::SLEEP);

  run_loop_until_done();
  EXPECT_TRUE(display_.is_sleeping());
}

TEST_F(EpaperSpectra6133ComponentTest, AutoSleepSleepsAfterFullRefresh) {
  display_.set_auto_sleep(true);
  reset_transport_state();

  display_.flush();
  run_loop_until_done();

  EXPECT_TRUE(display_.is_sleeping());
  EXPECT_EQ(count_register_writes(transport_state().operations, DSLP), 1U);
}

TEST_F(EpaperSpectra6133ComponentTest, AutoSleepSleepsAfterPartialRefresh) {
  display_.set_auto_sleep(true);
  reset_transport_state();

  display_.flush_region(10, 20, 80, 60);
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
