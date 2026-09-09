// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Exercises the hardware-flag path of iree_hal_hw_emulator_semaphore_wait
// directly: kicks off hw_execute_async() against a real hw_device and waits
// on the semaphore *without ever calling iree_hal_semaphore_signal*, proving
// that completion is genuinely observed via hw_query_execution_status()
// rather than the generic host-signaled path (already covered by
// semaphore_smoke_test.c). This talks to hw_execute_async/semaphore_create
// directly rather than through a full device, since queue_execute (which
// will eventually do this for real) isn't implemented yet.

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "hardwareEmulator.h"
#include "hw_runtime_plugin/semaphore.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

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

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  iree_allocator_t host_allocator = iree_allocator_system();

  hw_device_t* hw_device = NULL;
  hw_status_t hw_status =
      hw_init_hardware(/*device_id=*/0, /*memory_size=*/4096, &hw_device);
  IREE_CHECK_OK(hw_status_is_ok(hw_status)
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "hw_init_hardware failed: %s",
                                       hw_status.message));

  iree_async_proactor_pool_t* proactor_pool = NULL;
  IREE_CHECK_OK(iree_async_proactor_pool_create(
      /*node_count=*/1, /*node_ids=*/NULL,
      iree_async_proactor_pool_options_default(), host_allocator,
      &proactor_pool));
  iree_async_proactor_t* proactor = NULL;
  IREE_CHECK_OK(
      iree_async_proactor_pool_get(proactor_pool, 0, &proactor));

  iree_hal_semaphore_t* semaphore = NULL;
  IREE_CHECK_OK(iree_hal_hw_emulator_semaphore_create(
      proactor, IREE_HAL_QUEUE_AFFINITY_ANY, /*initial_value=*/0,
      IREE_HAL_SEMAPHORE_FLAG_NONE, hw_device, host_allocator, &semaphore));

  // Kick off async "device" execution -- standing in for what queue_execute
  // will eventually do -- and wait for it. No iree_hal_semaphore_signal call
  // happens anywhere in this test: the wait must be satisfied purely by
  // wait() observing hw_query_execution_status() flip to COMPLETE.
  hw_status = hw_execute_async(hw_device, slow_ok_kernel, /*inputs=*/NULL,
                               /*num_inputs=*/0, /*outputs=*/NULL,
                               /*num_outputs=*/0, /*params=*/NULL);
  IREE_CHECK_OK(hw_status_is_ok(hw_status)
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "hw_execute_async failed: %s",
                                       hw_status.message));
  IREE_CHECK_OK(iree_hal_semaphore_wait(semaphore, /*value=*/1,
                                        iree_infinite_timeout(),
                                        IREE_ASYNC_WAIT_FLAG_NONE));
  uint64_t value = 0;
  IREE_CHECK_OK(iree_hal_semaphore_query(semaphore, &value));
  IREE_CHECK_OK(value == 1
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "expected value 1 after hw completion"));

  // A failing async execution should surface as a failed wait, and the
  // failure should stick on subsequent query() calls too.
  hw_status = hw_execute_async(hw_device, failing_kernel, /*inputs=*/NULL,
                               /*num_inputs=*/0, /*outputs=*/NULL,
                               /*num_outputs=*/0, /*params=*/NULL);
  IREE_CHECK_OK(hw_status_is_ok(hw_status)
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "hw_execute_async failed: %s",
                                       hw_status.message));
  iree_status_t wait_status = iree_hal_semaphore_wait(
      semaphore, /*value=*/2, iree_infinite_timeout(), IREE_ASYNC_WAIT_FLAG_NONE);
  bool wait_failed_as_expected = !iree_status_is_ok(wait_status);
  iree_status_ignore(wait_status);
  IREE_CHECK_OK(wait_failed_as_expected
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "expected wait() to report the "
                                       "kernel failure"));
  iree_status_t query_status = iree_hal_semaphore_query(semaphore, &value);
  bool query_failed_as_expected = !iree_status_is_ok(query_status);
  iree_status_ignore(query_status);
  IREE_CHECK_OK(query_failed_as_expected
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "expected query() to keep reporting "
                                       "the failure"));

  iree_hal_semaphore_release(semaphore);
  iree_async_proactor_pool_release(proactor_pool);
  hw_destroy_hardware(hw_device);

  fprintf(stdout, "hw_emulator semaphore hardware-flag smoke test passed\n");
  return 0;
}
