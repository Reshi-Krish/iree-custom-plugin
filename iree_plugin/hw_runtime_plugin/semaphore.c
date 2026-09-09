// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/semaphore.h"

#include <time.h>

#include "hw_runtime_plugin/status_util.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_semaphore_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_hw_emulator_semaphore_t {
  iree_async_semaphore_t async;
  iree_allocator_t host_allocator;

  // Borrowed from the owning device. See semaphore.h.
  hw_device_t* hw_device;
} iree_hal_hw_emulator_semaphore_t;

static const iree_hal_semaphore_vtable_t iree_hal_hw_emulator_semaphore_vtable;

static iree_hal_hw_emulator_semaphore_t* iree_hal_hw_emulator_semaphore_cast(
    iree_hal_semaphore_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_semaphore_vtable);
  return (iree_hal_hw_emulator_semaphore_t*)base_value;
}

iree_status_t iree_hal_hw_emulator_semaphore_create(
    iree_async_proactor_t* proactor, iree_hal_queue_affinity_t queue_affinity,
    uint64_t initial_value, iree_hal_semaphore_flags_t flags,
    hw_device_t* hw_device, iree_allocator_t host_allocator,
    iree_hal_semaphore_t** out_semaphore) {
  IREE_ASSERT_ARGUMENT(proactor);
  IREE_ASSERT_ARGUMENT(hw_device);
  IREE_ASSERT_ARGUMENT(out_semaphore);
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_semaphore = NULL;

  iree_hal_hw_emulator_semaphore_t* semaphore = NULL;
  iree_host_size_t frontier_offset = 0, total_size = 0;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_async_semaphore_layout(sizeof(*semaphore), 0, &frontier_offset,
                                      &total_size));
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0,
      iree_allocator_malloc(host_allocator, total_size, (void**)&semaphore));
  iree_async_semaphore_initialize(
      (const iree_async_semaphore_vtable_t*)&iree_hal_hw_emulator_semaphore_vtable,
      proactor, initial_value, frontier_offset, 0, &semaphore->async);
  semaphore->host_allocator = host_allocator;
  semaphore->hw_device = hw_device;

  *out_semaphore = iree_hal_semaphore_cast(&semaphore->async);
  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

static void iree_hal_hw_emulator_semaphore_destroy(
    iree_async_semaphore_t* base_semaphore) {
  iree_hal_hw_emulator_semaphore_t* semaphore =
      iree_hal_hw_emulator_semaphore_cast(iree_hal_semaphore_cast(base_semaphore));
  iree_allocator_t host_allocator = semaphore->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_async_semaphore_deinitialize(&semaphore->async);
  iree_allocator_free(host_allocator, semaphore);

  IREE_TRACE_ZONE_END(z0);
}

static uint64_t iree_hal_hw_emulator_semaphore_query(
    iree_async_semaphore_t* base_semaphore) {
  // Both fields are atomic on the shared iree_async_semaphore_t base: fully
  // lock-free query, no derived-type cast needed.
  iree_status_t failure = (iree_status_t)iree_atomic_load(
      &base_semaphore->failure_status, iree_memory_order_acquire);
  if (!iree_status_is_ok(failure)) {
    return iree_hal_status_as_semaphore_failure(failure);
  }
  return (uint64_t)iree_atomic_load(&base_semaphore->timeline_value,
                                    iree_memory_order_acquire);
}

static iree_status_t iree_hal_hw_emulator_semaphore_signal(
    iree_async_semaphore_t* base_semaphore, uint64_t new_value,
    const iree_async_frontier_t* frontier) {
  // Advance the timeline (validates new_value > current, merges frontier) and
  // wake any timepoints it satisfies. iree_async_semaphore_multi_wait
  // (used by wait() below) registers the timepoints this dispatches.
  IREE_RETURN_IF_ERROR(iree_async_semaphore_advance_timeline(
      base_semaphore, new_value, frontier));
  iree_async_semaphore_dispatch_timepoints(base_semaphore, new_value);
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_semaphore_wait(
    iree_hal_semaphore_t* base_semaphore, uint64_t value,
    iree_timeout_t timeout, iree_async_wait_flags_t flags) {
  iree_hal_hw_emulator_semaphore_t* semaphore =
      iree_hal_hw_emulator_semaphore_cast(base_semaphore);
  (void)flags;

  const iree_time_t deadline_ns = iree_timeout_as_deadline_ns(timeout);

  // Polls two independent completion sources each iteration:
  //  (a) the generic host-signaled timeline (iree_hal_semaphore_signal called
  //      directly, from any thread), and
  //  (b) the hw_emulator's execution-status flag -- a stand-in for a real
  //      device's completion/doorbell register, set by a background thread
  //      running the dispatched kernel (see hw_execute_async).
  // There's no push-based wakeup for (b) (the background thread only flips
  // the flag, it does not call back into IREE), so this can't just register
  // a timepoint and block like the generic iree_async_semaphore_multi_wait
  // path does -- polling is the actual notification mechanism here, same as
  // it would be against a real memory-mapped status register without an
  // interrupt.
  //
  // NOTE: hw_query_execution_status() reports one global flag per hw_device,
  // not per dispatch, and hw_execute_async() refuses a second submission
  // while one is RUNNING. That's fine for today's one-dispatch-at-a-time
  // model (there's only ever one outstanding completion to observe, so
  // advancing straight to |value| below is correct) but will need revisiting
  // -- e.g. tagging each async execution with the target value it should
  // complete to -- once more than one dispatch can be in flight at a time.
  for (;;) {
    uint64_t current =
        iree_async_semaphore_query((iree_async_semaphore_t*)base_semaphore);
    if (current >= IREE_HAL_SEMAPHORE_FAILURE_VALUE) {
      return iree_hal_semaphore_failure_as_status(current);
    }
    if (current >= value) return iree_ok_status();

    hw_status_t hw_result;
    hw_execution_status_t hw_status =
        hw_query_execution_status(semaphore->hw_device, &hw_result);
    if (hw_status == HW_EXECUTION_COMPLETE) {
      IREE_RETURN_IF_ERROR(iree_async_semaphore_advance_timeline(
          (iree_async_semaphore_t*)base_semaphore, value, NULL));
      iree_async_semaphore_dispatch_timepoints(
          (iree_async_semaphore_t*)base_semaphore, value);
      return iree_ok_status();
    } else if (hw_status == HW_EXECUTION_FAILED) {
      iree_status_t failure_status =
          iree_hal_hw_emulator_status_from_hw(hw_result);
      iree_async_semaphore_fail((iree_async_semaphore_t*)base_semaphore,
                                iree_status_clone(failure_status));
      return failure_status;
    }

    if (iree_timeout_is_immediate(timeout)) {
      return iree_status_from_code(IREE_STATUS_DEADLINE_EXCEEDED);
    }
    if (deadline_ns != IREE_TIME_INFINITE_FUTURE &&
        iree_time_now() >= deadline_ns) {
      return iree_status_from_code(IREE_STATUS_DEADLINE_EXCEEDED);
    }
    struct timespec poll_delay = {.tv_sec = 0, .tv_nsec = 1 * 1000 * 1000};
    nanosleep(&poll_delay, NULL);
  }
}

static iree_status_t iree_hal_hw_emulator_semaphore_import_timepoint(
    iree_hal_semaphore_t* base_semaphore, uint64_t value,
    iree_hal_queue_affinity_t queue_affinity,
    iree_hal_external_timepoint_t external_timepoint) {
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "timepoint import is not yet implemented");
}

static iree_status_t iree_hal_hw_emulator_semaphore_export_timepoint(
    iree_hal_semaphore_t* base_semaphore, uint64_t value,
    iree_hal_queue_affinity_t queue_affinity,
    iree_hal_external_timepoint_type_t requested_type,
    iree_hal_external_timepoint_flags_t requested_flags,
    iree_hal_external_timepoint_t* IREE_RESTRICT out_external_timepoint) {
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "timepoint export is not yet implemented");
}

static const iree_hal_semaphore_vtable_t iree_hal_hw_emulator_semaphore_vtable = {
    .async =
        {
            .destroy = iree_hal_hw_emulator_semaphore_destroy,
            .query = iree_hal_hw_emulator_semaphore_query,
            .signal = iree_hal_hw_emulator_semaphore_signal,
        },
    .wait = iree_hal_hw_emulator_semaphore_wait,
    .import_timepoint = iree_hal_hw_emulator_semaphore_import_timepoint,
    .export_timepoint = iree_hal_hw_emulator_semaphore_export_timepoint,
};
