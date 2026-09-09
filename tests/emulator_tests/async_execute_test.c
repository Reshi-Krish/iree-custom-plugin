/*
 * Exercises hw_execute_async()/hw_query_execution_status(): submits a
 * deliberately slow kernel on the background thread and confirms (a) the
 * call returns immediately while the kernel is still running, (b) the status
 * flag correctly reports RUNNING then COMPLETE, and (c) a failing kernel is
 * reported as HW_EXECUTION_FAILED with its status preserved.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -pthread \
 *     -I../../hardware_emulator/include \
 *     ../../hardware_emulator/src/hardwareEmulator.c \
 *     async_execute_test.c -o async_execute_test
 * Run:
 *   ./async_execute_test
 */

#include "hardwareEmulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static hw_status_t slow_ok_kernel(void* const* inputs, size_t num_inputs,
                                  void* const* outputs, size_t num_outputs,
                                  void* params) {
  (void)inputs;
  (void)num_inputs;
  (void)outputs;
  (void)num_outputs;
  (void)params;
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};  // 100ms
  nanosleep(&delay, NULL);
  return hw_status_ok();
}

static hw_status_t failing_kernel(void* const* inputs, size_t num_inputs,
                                  void* const* outputs, size_t num_outputs,
                                  void* params) {
  (void)inputs;
  (void)num_inputs;
  (void)outputs;
  (void)num_outputs;
  (void)params;
  hw_status_t status;
  status.code = HW_STATUS_INTERNAL_ERROR;
  status.message = "synthetic failure for testing";
  return status;
}

static hw_execution_status_t poll_until_done(hw_device_t* device,
                                             hw_status_t* out_result,
                                             int timeout_ms) {
  const int kPollIntervalMs = 5;
  for (int waited_ms = 0; waited_ms <= timeout_ms; waited_ms += kPollIntervalMs) {
    hw_execution_status_t status = hw_query_execution_status(device, out_result);
    if (status == HW_EXECUTION_COMPLETE || status == HW_EXECUTION_FAILED) {
      return status;
    }
    struct timespec delay = {.tv_sec = 0,
                             .tv_nsec = kPollIntervalMs * 1000 * 1000};
    nanosleep(&delay, NULL);
  }
  return HW_EXECUTION_RUNNING;  // timed out still running
}

int main(void) {
  hw_device_t* device = NULL;
  hw_status_t status =
      hw_init_hardware(/*device_id=*/0, /*memory_size=*/4096, &device);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_init_hardware failed: %s\n",
            status.message ? status.message : "(no message)");
    return EXIT_FAILURE;
  }

  // Starts idle.
  if (hw_query_execution_status(device, NULL) != HW_EXECUTION_IDLE) {
    fprintf(stderr, "expected HW_EXECUTION_IDLE before any submission\n");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  // Submit the slow kernel and confirm the call returned before it finished.
  status = hw_execute_async(device, slow_ok_kernel, /*inputs=*/NULL,
                            /*num_inputs=*/0, /*outputs=*/NULL,
                            /*num_outputs=*/0, /*params=*/NULL);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_execute_async(slow_ok_kernel) failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }
  hw_execution_status_t immediate_status =
      hw_query_execution_status(device, NULL);
  if (immediate_status != HW_EXECUTION_RUNNING) {
    fprintf(stderr,
            "expected HW_EXECUTION_RUNNING immediately after "
            "hw_execute_async (kernel sleeps 100ms); got %d -- "
            "execution did not appear to run asynchronously\n",
            (int)immediate_status);
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  hw_status_t result;
  hw_execution_status_t final_status =
      poll_until_done(device, &result, /*timeout_ms=*/2000);
  if (final_status != HW_EXECUTION_COMPLETE) {
    fprintf(stderr, "slow_ok_kernel did not complete successfully (status=%d)\n",
            (int)final_status);
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  // A second, failing async execution should report HW_EXECUTION_FAILED with
  // the kernel's status preserved.
  status = hw_execute_async(device, failing_kernel, /*inputs=*/NULL,
                            /*num_inputs=*/0, /*outputs=*/NULL,
                            /*num_outputs=*/0, /*params=*/NULL);
  if (!hw_status_is_ok(status)) {
    fprintf(stderr, "hw_execute_async(failing_kernel) failed: %s\n",
            status.message ? status.message : "(no message)");
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }
  final_status = poll_until_done(device, &result, /*timeout_ms=*/2000);
  if (final_status != HW_EXECUTION_FAILED ||
      result.code != HW_STATUS_INTERNAL_ERROR) {
    fprintf(stderr, "expected HW_EXECUTION_FAILED/HW_STATUS_INTERNAL_ERROR, "
                    "got status=%d code=%d\n",
            (int)final_status, (int)result.code);
    hw_destroy_hardware(device);
    return EXIT_FAILURE;
  }

  hw_destroy_hardware(device);
  printf("[PASS] async_execute_test\n");
  return EXIT_SUCCESS;
}
