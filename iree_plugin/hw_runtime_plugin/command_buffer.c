// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/command_buffer.h"

#include <time.h>

#include "hw_runtime_plugin/buffer.h"
#include "hw_runtime_plugin/executable.h"
#include "hw_runtime_plugin/status_util.h"
#include "iree/hal/utils/deferred_command_buffer.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_command_buffer_t (recorder)
//===----------------------------------------------------------------------===//
// There is nothing hardware-specific about *recording* commands, only about
// *applying* them (see the applier below), so this is a thin pass-through to
// IREE's generic deferred command buffer.

iree_status_t iree_hal_hw_emulator_command_buffer_create(
    iree_hal_allocator_t* device_allocator, iree_hal_command_buffer_mode_t mode,
    iree_hal_command_category_t command_categories,
    iree_hal_queue_affinity_t queue_affinity, iree_host_size_t binding_capacity,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    iree_hal_command_buffer_t** out_command_buffer) {
  return iree_hal_deferred_command_buffer_create(
      device_allocator, mode, command_categories, queue_affinity,
      binding_capacity, block_pool, host_allocator, out_command_buffer);
}

bool iree_hal_hw_emulator_command_buffer_isa(
    iree_hal_command_buffer_t* command_buffer) {
  return iree_hal_deferred_command_buffer_isa(command_buffer);
}

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_applier_t
//===----------------------------------------------------------------------===//
// The applier is the target of iree_hal_deferred_command_buffer_apply: it
// executes each recorded command immediately, synchronously, in order. Every
// command except dispatch is a plain host-memory operation (fill/copy/update
// map straight onto IREE's generic host-buffer helpers); dispatch is the one
// truly hardware-specific piece, and even that only differs from the others
// in that it runs the kernel on hw_execute_async's background thread and
// blocks until it completes before returning - the emulator has no notion of
// overlapping dispatches yet (see hardwareEmulator.h).

typedef struct iree_hal_hw_emulator_applier_t {
  iree_hal_command_buffer_t base;
  iree_allocator_t host_allocator;

  // Borrowed from the owning device.
  hw_device_t* hw_device;
} iree_hal_hw_emulator_applier_t;

static const iree_hal_command_buffer_vtable_t iree_hal_hw_emulator_applier_vtable;

static iree_hal_hw_emulator_applier_t* iree_hal_hw_emulator_applier_cast(
    iree_hal_command_buffer_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_applier_vtable);
  return (iree_hal_hw_emulator_applier_t*)base_value;
}

iree_status_t iree_hal_hw_emulator_applier_create(
    hw_device_t* hw_device, iree_allocator_t host_allocator,
    iree_hal_command_buffer_t** out_command_buffer) {
  IREE_ASSERT_ARGUMENT(hw_device);
  IREE_ASSERT_ARGUMENT(out_command_buffer);
  *out_command_buffer = NULL;

  iree_hal_hw_emulator_applier_t* applier = NULL;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(host_allocator, sizeof(*applier),
                                             (void**)&applier));
  // UNVALIDATED: the applier is never exposed to user code (only fed
  // trusted, already-recorded commands from queue_execute), so there's
  // nothing useful for the validation layer to check here.
  iree_hal_command_buffer_initialize(
      /*device_allocator=*/NULL,
      IREE_HAL_COMMAND_BUFFER_MODE_ONE_SHOT |
          IREE_HAL_COMMAND_BUFFER_MODE_UNVALIDATED,
      IREE_HAL_COMMAND_CATEGORY_ANY, IREE_HAL_QUEUE_AFFINITY_ANY,
      /*binding_capacity=*/0, /*validation_state=*/NULL,
      &iree_hal_hw_emulator_applier_vtable, &applier->base);
  applier->host_allocator = host_allocator;
  applier->hw_device = hw_device;

  *out_command_buffer = &applier->base;
  return iree_ok_status();
}

static void iree_hal_hw_emulator_applier_destroy(
    iree_hal_command_buffer_t* base_command_buffer) {
  iree_hal_hw_emulator_applier_t* applier =
      iree_hal_hw_emulator_applier_cast(base_command_buffer);
  iree_allocator_free(applier->host_allocator, applier);
}

static iree_status_t iree_hal_hw_emulator_applier_begin(
    iree_hal_command_buffer_t* base_command_buffer) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_end(
    iree_hal_command_buffer_t* base_command_buffer) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_begin_debug_group(
    iree_hal_command_buffer_t* base_command_buffer, iree_string_view_t label,
    iree_hal_label_color_t label_color,
    const iree_hal_label_location_t* location) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_end_debug_group(
    iree_hal_command_buffer_t* base_command_buffer) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_execution_barrier(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_execution_stage_t source_stage_mask,
    iree_hal_execution_stage_t target_stage_mask,
    iree_hal_execution_barrier_flags_t flags,
    iree_host_size_t memory_barrier_count,
    const iree_hal_memory_barrier_t* memory_barriers,
    iree_host_size_t buffer_barrier_count,
    const iree_hal_buffer_barrier_t* buffer_barriers) {
  // The applier already executes every command synchronously in strict
  // recorded order (including blocking until a dispatch's kernel finishes),
  // so a barrier is trivially already satisfied by the time it is reached.
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_signal_event(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_event_t* event,
    iree_hal_execution_stage_t source_stage_mask) {
  // Same reasoning as execution_barrier: strict sequential execution means
  // anything that would wait on this event later will find it already
  // satisfied, so there's nothing to actually track.
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_reset_event(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_event_t* event,
    iree_hal_execution_stage_t source_stage_mask) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_wait_events(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_host_size_t event_count, const iree_hal_event_t** events,
    iree_hal_execution_stage_t source_stage_mask,
    iree_hal_execution_stage_t target_stage_mask,
    iree_host_size_t memory_barrier_count,
    const iree_hal_memory_barrier_t* memory_barriers,
    iree_host_size_t buffer_barrier_count,
    const iree_hal_buffer_barrier_t* buffer_barriers) {
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_advise_buffer(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_buffer_ref_t buffer_ref, iree_hal_memory_advise_flags_t flags,
    uint64_t arg0, uint64_t arg1) {
  // Pure hint; nothing to prefetch/discard in the emulator.
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_applier_fill_buffer(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_buffer_ref_t target_ref, const void* pattern,
    iree_host_size_t pattern_length, iree_hal_fill_flags_t flags) {
  return iree_hal_buffer_map_fill(target_ref.buffer, target_ref.offset,
                                  target_ref.length, pattern, pattern_length);
}

static iree_status_t iree_hal_hw_emulator_applier_update_buffer(
    iree_hal_command_buffer_t* base_command_buffer, const void* source_buffer,
    iree_host_size_t source_offset, iree_hal_buffer_ref_t target_ref,
    iree_hal_update_flags_t flags) {
  return iree_hal_buffer_map_write(
      target_ref.buffer, target_ref.offset,
      (const uint8_t*)source_buffer + source_offset, target_ref.length);
}

static iree_status_t iree_hal_hw_emulator_applier_copy_buffer(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_buffer_ref_t source_ref, iree_hal_buffer_ref_t target_ref,
    iree_hal_copy_flags_t flags) {
  return iree_hal_buffer_map_copy(source_ref.buffer, source_ref.offset,
                                  target_ref.buffer, target_ref.offset,
                                  target_ref.length);
}

static iree_status_t iree_hal_hw_emulator_applier_collective(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_channel_t* channel,
    iree_hal_collective_op_t op, uint32_t param, iree_hal_buffer_ref_t send_ref,
    iree_hal_buffer_ref_t recv_ref, iree_device_size_t element_count) {
  // No multi-device story yet for a single emulated accelerator.
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "collectives not implemented");
}

static iree_status_t iree_hal_hw_emulator_applier_dispatch(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_executable_t* executable,
    iree_hal_executable_function_t export_ordinal,
    const iree_hal_dispatch_config_t config, iree_const_byte_span_t constants,
    iree_hal_buffer_ref_list_t bindings, iree_hal_dispatch_flags_t flags) {
  iree_hal_hw_emulator_applier_t* applier =
      iree_hal_hw_emulator_applier_cast(base_command_buffer);

  // Our kernel ABI (hw_kernel_fn_t) has no notion of a workgroup grid - a
  // dispatch is always exactly one kernel invocation over the full bindings -
  // so config.workgroup_count/workgroup_size are intentionally unused here.
  // NOTE: callers must still set workgroup_count to something non-zero (e.g.
  // {1,1,1}): iree_hal_command_buffer_dispatch treats an all-zero count as an
  // intentional no-op and skips calling us entirely.
  if (iree_hal_dispatch_uses_indirect_parameters(flags) ||
      iree_hal_dispatch_uses_custom_arguments(flags)) {
    return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                            "the hw_emulator applier only supports direct "
                            "dispatch with standard HAL ABI arguments");
  }

  const iree_hal_hw_emulator_kernel_registration_t* export_entry = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_executable_lookup_export(
      executable, export_ordinal, &export_entry));
  const iree_host_size_t expected_bindings =
      export_entry->num_inputs + export_entry->num_outputs;
  if (bindings.count != expected_bindings) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "binding count mismatch for export '%.*s': expected %" PRIhsz
        " (%" PRIhsz " inputs + %" PRIhsz " outputs) but was provided %" PRIhsz,
        (int)export_entry->name.size, export_entry->name.data,
        expected_bindings, export_entry->num_inputs, export_entry->num_outputs,
        bindings.count);
  }

  enum { kMaxBuffers = 16 };
  if (export_entry->num_inputs > kMaxBuffers ||
      export_entry->num_outputs > kMaxBuffers) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "too many input/output buffers for the hw_emulator applier");
  }

  // Resolve each binding to a hw_buffer_t (an offset/size pair into the
  // device's memory pool, NOT a host pointer - hw_execute_async does its own
  // hw_buffer_map() resolution, same as real hardware resolving its own
  // addresses). Bindings are ordered inputs-then-outputs by convention.
  hw_buffer_t input_handles[kMaxBuffers];
  hw_buffer_t output_handles[kMaxBuffers];
  for (iree_host_size_t i = 0; i < bindings.count; ++i) {
    hw_buffer_t handle;
    IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_buffer_handle(
        bindings.values[i].buffer, bindings.values[i].offset,
        bindings.values[i].length, &handle));
    if (i < export_entry->num_inputs) {
      input_handles[i] = handle;
    } else {
      output_handles[i - export_entry->num_inputs] = handle;
    }
  }

  IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_status_from_hw(hw_execute_async(
      applier->hw_device, export_entry->kernel, input_handles,
      export_entry->num_inputs, output_handles, export_entry->num_outputs,
      (void*)constants.data)));

  // Block until the kernel actually finishes: hw_execute_async only supports
  // one execution in flight per device (see hardwareEmulator.h), and
  // queue_execute's contract is that the work is done by the time it returns
  // and signals completion - so there is nothing to gain from returning
  // early here.
  for (;;) {
    hw_status_t result;
    hw_execution_status_t status =
        hw_query_execution_status(applier->hw_device, &result);
    if (status == HW_EXECUTION_COMPLETE) return iree_ok_status();
    if (status == HW_EXECUTION_FAILED) {
      return iree_hal_hw_emulator_status_from_hw(result);
    }
    struct timespec poll_delay = {.tv_sec = 0, .tv_nsec = 1 * 1000 * 1000};
    nanosleep(&poll_delay, NULL);
  }
}

static const iree_hal_command_buffer_vtable_t iree_hal_hw_emulator_applier_vtable = {
    .destroy = iree_hal_hw_emulator_applier_destroy,
    .begin = iree_hal_hw_emulator_applier_begin,
    .end = iree_hal_hw_emulator_applier_end,
    .begin_debug_group = iree_hal_hw_emulator_applier_begin_debug_group,
    .end_debug_group = iree_hal_hw_emulator_applier_end_debug_group,
    .execution_barrier = iree_hal_hw_emulator_applier_execution_barrier,
    .signal_event = iree_hal_hw_emulator_applier_signal_event,
    .reset_event = iree_hal_hw_emulator_applier_reset_event,
    .wait_events = iree_hal_hw_emulator_applier_wait_events,
    .advise_buffer = iree_hal_hw_emulator_applier_advise_buffer,
    .fill_buffer = iree_hal_hw_emulator_applier_fill_buffer,
    .update_buffer = iree_hal_hw_emulator_applier_update_buffer,
    .copy_buffer = iree_hal_hw_emulator_applier_copy_buffer,
    .collective = iree_hal_hw_emulator_applier_collective,
    .dispatch = iree_hal_hw_emulator_applier_dispatch,
};
