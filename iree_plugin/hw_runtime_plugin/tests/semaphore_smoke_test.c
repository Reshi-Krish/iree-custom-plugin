// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// End-to-end smoke test for the hw_emulator semaphore: query/signal, an
// immediate-timeout wait on an unmet value, and - the trickiest part to get
// right - an actual cross-thread blocking wait that is woken by a signal from
// another thread through iree/async's timepoint dispatch machinery.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hw_runtime_plugin/api.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/base/api.h"
#include "iree/base/threading/thread.h"
#include "iree/hal/api.h"

typedef struct signaler_args_t {
  iree_hal_semaphore_t* semaphore;
  uint64_t signal_value;
} signaler_args_t;

// Gives the main thread a chance to call iree_hal_semaphore_wait and register
// its timepoint before we signal, so the wait exercises the real blocking/
// wake path instead of always hitting the fast already-satisfied check.
static int signaler_thread_main(void* entry_arg) {
  signaler_args_t* args = (signaler_args_t*)entry_arg;
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};  // 100ms
  nanosleep(&delay, NULL);
  IREE_CHECK_OK(
      iree_hal_semaphore_signal(args->semaphore, args->signal_value, NULL));
  return 0;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  iree_allocator_t host_allocator = iree_allocator_system();

  iree_async_proactor_pool_t* proactor_pool = NULL;
  IREE_CHECK_OK(iree_async_proactor_pool_create(
      /*node_count=*/1, /*node_ids=*/NULL,
      iree_async_proactor_pool_options_default(), host_allocator,
      &proactor_pool));

  iree_hal_hw_emulator_driver_options_t driver_options;
  iree_hal_hw_emulator_driver_options_initialize(&driver_options);
  iree_hal_driver_t* driver = NULL;
  IREE_CHECK_OK(iree_hal_hw_emulator_driver_create(
      IREE_SV("hw_emulator"), &driver_options, host_allocator, &driver));

  iree_hal_device_create_params_t create_params =
      iree_hal_device_create_params_default();
  create_params.proactor_pool = proactor_pool;
  iree_hal_device_t* device = NULL;
  IREE_CHECK_OK(iree_hal_driver_create_default_device(
      driver, &create_params, host_allocator, &device));
  iree_async_proactor_pool_release(proactor_pool);

  iree_hal_semaphore_t* semaphore = NULL;
  IREE_CHECK_OK(iree_hal_semaphore_create(device, IREE_HAL_QUEUE_AFFINITY_ANY,
                                          /*initial_value=*/0,
                                          IREE_HAL_SEMAPHORE_FLAG_NONE,
                                          &semaphore));

  // Starts at 0.
  uint64_t value = 0;
  IREE_CHECK_OK(iree_hal_semaphore_query(semaphore, &value));
  IREE_CHECK_OK(value == 0 ? iree_ok_status()
                           : iree_make_status(IREE_STATUS_INTERNAL,
                                              "expected initial value 0"));

  // An immediate wait on an unmet value must fail with DEADLINE_EXCEEDED,
  // not block.
  iree_status_t wait_status =
      iree_hal_semaphore_wait(semaphore, /*value=*/1, iree_immediate_timeout(),
                              IREE_ASYNC_WAIT_FLAG_NONE);
  bool immediate_wait_correct = iree_status_is_deadline_exceeded(wait_status);
  iree_status_ignore(wait_status);
  IREE_CHECK_OK(immediate_wait_correct
                    ? iree_ok_status()
                    : iree_make_status(IREE_STATUS_INTERNAL,
                                       "expected DEADLINE_EXCEEDED"));

  // Signal to 1 synchronously and confirm query + an already-satisfied wait.
  IREE_CHECK_OK(iree_hal_semaphore_signal(semaphore, 1, NULL));
  IREE_CHECK_OK(iree_hal_semaphore_query(semaphore, &value));
  IREE_CHECK_OK(value == 1 ? iree_ok_status()
                           : iree_make_status(IREE_STATUS_INTERNAL,
                                              "expected value 1 after signal"));
  IREE_CHECK_OK(iree_hal_semaphore_wait(semaphore, 1, iree_infinite_timeout(),
                                        IREE_ASYNC_WAIT_FLAG_NONE));

  // Now exercise the real cross-thread blocking wake path: a second thread
  // signals to 2 shortly after this thread starts blocking on it.
  signaler_args_t signaler_args = {.semaphore = semaphore, .signal_value = 2};
  iree_thread_create_params_t thread_params;
  memset(&thread_params, 0, sizeof(thread_params));
  iree_thread_t* signaler_thread = NULL;
  IREE_CHECK_OK(iree_thread_create(signaler_thread_main, &signaler_args,
                                   thread_params, host_allocator,
                                   &signaler_thread));
  IREE_CHECK_OK(iree_hal_semaphore_wait(semaphore, 2, iree_infinite_timeout(),
                                        IREE_ASYNC_WAIT_FLAG_NONE));
  iree_thread_join(signaler_thread);
  iree_thread_release(signaler_thread);
  IREE_CHECK_OK(iree_hal_semaphore_query(semaphore, &value));
  IREE_CHECK_OK(
      value == 2 ? iree_ok_status()
                : iree_make_status(IREE_STATUS_INTERNAL,
                                   "expected value 2 after cross-thread wait"));

  iree_hal_semaphore_release(semaphore);
  iree_hal_device_release(device);
  iree_hal_driver_release(driver);

  fprintf(stdout, "hw_emulator semaphore smoke test passed\n");
  return 0;
}
