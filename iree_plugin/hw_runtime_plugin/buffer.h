// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_BUFFER_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_BUFFER_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_buffer_t
//===----------------------------------------------------------------------===//

// Wraps a hw_emulator allocation in an iree_hal_buffer_t. |hw_device| and
// |hw_buffer| identify the backing region within the emulator's memory pool;
// |hw_device| is borrowed and must outlive the returned buffer.
iree_status_t iree_hal_hw_emulator_buffer_wrap(
    iree_hal_buffer_placement_t placement, iree_hal_memory_type_t memory_type,
    iree_hal_memory_access_t allowed_access,
    iree_hal_buffer_usage_t allowed_usage, iree_device_size_t allocation_size,
    iree_device_size_t byte_offset, iree_device_size_t byte_length,
    hw_device_t* hw_device, hw_buffer_t hw_buffer,
    iree_hal_buffer_release_callback_t release_callback,
    iree_allocator_t host_allocator, iree_hal_buffer_t** out_buffer);

// Returns the hw_emulator hardware buffer handle (an offset/size pair into
// the device's memory pool) backing the [local_byte_offset,
// local_byte_offset + local_byte_length) region of |base_buffer|, which must
// have been created by this driver's allocator. |local_byte_length| may be
// IREE_HAL_WHOLE_BUFFER. Used by command_buffer.c's applier to resolve
// dispatch bindings for hw_execute_async, which does its own hw_buffer_map()
// resolution and so needs the handle rather than a mapped host pointer.
iree_status_t iree_hal_hw_emulator_buffer_handle(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length, hw_buffer_t* out_hw_buffer);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_BUFFER_H_
