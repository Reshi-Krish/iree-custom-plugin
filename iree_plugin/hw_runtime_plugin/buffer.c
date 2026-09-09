// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/buffer.h"

#include "hw_runtime_plugin/status_util.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_buffer_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_hw_emulator_buffer_t {
  iree_hal_buffer_t base;
  iree_allocator_t host_allocator;
  iree_hal_buffer_release_callback_t release_callback;

  // Owning hw_device and this buffer's sub-allocation within its memory
  // pool. |hw_device| is borrowed: buffers must not be used after their
  // device is destroyed.
  hw_device_t* hw_device;
  hw_buffer_t hw_buffer;
} iree_hal_hw_emulator_buffer_t;

static const iree_hal_buffer_vtable_t iree_hal_hw_emulator_buffer_vtable;

static iree_hal_hw_emulator_buffer_t* iree_hal_hw_emulator_buffer_cast(
    iree_hal_buffer_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_buffer_vtable);
  return (iree_hal_hw_emulator_buffer_t*)base_value;
}

static const iree_hal_hw_emulator_buffer_t* iree_hal_hw_emulator_buffer_const_cast(
    const iree_hal_buffer_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_buffer_vtable);
  return (const iree_hal_hw_emulator_buffer_t*)base_value;
}

iree_status_t iree_hal_hw_emulator_buffer_wrap(
    iree_hal_buffer_placement_t placement, iree_hal_memory_type_t memory_type,
    iree_hal_memory_access_t allowed_access,
    iree_hal_buffer_usage_t allowed_usage, iree_device_size_t allocation_size,
    iree_device_size_t byte_offset, iree_device_size_t byte_length,
    hw_device_t* hw_device, hw_buffer_t hw_buffer,
    iree_hal_buffer_release_callback_t release_callback,
    iree_allocator_t host_allocator, iree_hal_buffer_t** out_buffer) {
  IREE_ASSERT_ARGUMENT(hw_device);
  IREE_ASSERT_ARGUMENT(out_buffer);
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_buffer = NULL;

  iree_hal_hw_emulator_buffer_t* buffer = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0,
      iree_allocator_malloc(host_allocator, sizeof(*buffer), (void**)&buffer));
  iree_hal_buffer_initialize(placement, &buffer->base, allocation_size,
                             byte_offset, byte_length, memory_type,
                             allowed_access, allowed_usage,
                             &iree_hal_hw_emulator_buffer_vtable, &buffer->base);
  buffer->host_allocator = host_allocator;
  buffer->release_callback = release_callback;
  buffer->hw_device = hw_device;
  buffer->hw_buffer = hw_buffer;

  *out_buffer = &buffer->base;
  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

static void iree_hal_hw_emulator_buffer_destroy(iree_hal_buffer_t* base_buffer) {
  iree_hal_hw_emulator_buffer_t* buffer = iree_hal_hw_emulator_buffer_cast(base_buffer);
  iree_allocator_t host_allocator = buffer->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  // Optionally call a release callback when the buffer is destroyed. Not all
  // implementations may require this but it's cheap and provides additional
  // flexibility.
  if (buffer->release_callback.fn) {
    buffer->release_callback.fn(buffer->release_callback.user_data,
                                base_buffer);
  }

  iree_allocator_free(host_allocator, buffer);

  IREE_TRACE_ZONE_END(z0);
}

static iree_status_t iree_hal_hw_emulator_buffer_map_range(
    iree_hal_buffer_t* base_buffer, iree_hal_mapping_mode_t mapping_mode,
    iree_hal_memory_access_t memory_access,
    iree_device_size_t local_byte_offset, iree_device_size_t local_byte_length,
    iree_hal_buffer_mapping_t* mapping) {
  iree_hal_hw_emulator_buffer_t* buffer = iree_hal_hw_emulator_buffer_cast(base_buffer);

  IREE_RETURN_IF_ERROR(iree_hal_buffer_validate_memory_type(
      iree_hal_buffer_memory_type(base_buffer),
      IREE_HAL_MEMORY_TYPE_HOST_VISIBLE));
  IREE_RETURN_IF_ERROR(iree_hal_buffer_validate_usage(
      iree_hal_buffer_allowed_usage(base_buffer),
      mapping_mode == IREE_HAL_MAPPING_MODE_PERSISTENT
          ? IREE_HAL_BUFFER_USAGE_MAPPING_PERSISTENT
          : IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED));

  void* base_ptr = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_status_from_hw(
      hw_buffer_map(buffer->hw_device, buffer->hw_buffer, &base_ptr)));
  mapping->contents = iree_make_byte_span(
      (uint8_t*)base_ptr + local_byte_offset, local_byte_length);

  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_buffer_unmap_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length, iree_hal_buffer_mapping_t* mapping) {
  // No-op: the emulator always resolves to a stable host pointer for the
  // lifetime of the buffer, so there is nothing to tear down here.
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_buffer_invalidate_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length) {
  // The emulator has a single unified memory pool with no separate
  // device-side cache; a fence is sufficient to observe prior writes.
  iree_atomic_thread_fence(iree_memory_order_acquire);
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_buffer_flush_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length) {
  iree_atomic_thread_fence(iree_memory_order_release);
  return iree_ok_status();
}

static const iree_hal_buffer_vtable_t iree_hal_hw_emulator_buffer_vtable = {
    .recycle = iree_hal_buffer_recycle,
    .destroy = iree_hal_hw_emulator_buffer_destroy,
    .map_range = iree_hal_hw_emulator_buffer_map_range,
    .unmap_range = iree_hal_hw_emulator_buffer_unmap_range,
    .invalidate_range = iree_hal_hw_emulator_buffer_invalidate_range,
    .flush_range = iree_hal_hw_emulator_buffer_flush_range,
};
