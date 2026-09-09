// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_COMMAND_BUFFER_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_COMMAND_BUFFER_H_

#include "hardwareEmulator.h"
#include "iree/base/internal/arena.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_command_buffer_t
//===----------------------------------------------------------------------===//

// Creates a hw_emulator command buffer that *records* commands for later
// replay (it does not execute anything itself). This is a thin wrapper
// around IREE's generic iree_hal_deferred_command_buffer_create - there is
// nothing hardware-specific about recording, only about applying (see the
// applier below), which is what queue_execute (device.c) uses at submission
// time via iree_hal_deferred_command_buffer_apply.
// |block_pool| is borrowed from the owning device and must outlive every
// command buffer created from it.
iree_status_t iree_hal_hw_emulator_command_buffer_create(
    iree_hal_allocator_t* device_allocator, iree_hal_command_buffer_mode_t mode,
    iree_hal_command_category_t command_categories,
    iree_hal_queue_affinity_t queue_affinity, iree_host_size_t binding_capacity,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    iree_hal_command_buffer_t** out_command_buffer);

// Returns true if |command_buffer| is a hw_emulator (deferred) command buffer.
bool iree_hal_hw_emulator_command_buffer_isa(
    iree_hal_command_buffer_t* command_buffer);

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_applier_t
//===----------------------------------------------------------------------===//

// Creates the "applier": an iree_hal_command_buffer_t that *executes*
// immediately against |hw_device| rather than recording, one command at a
// time, synchronously (including blocking until any dispatched kernel
// finishes on hw_execute_async's background thread). Intended to be created
// once per device and reused as the target of repeated
// iree_hal_deferred_command_buffer_apply calls from queue_execute.
// |hw_device| is borrowed and must outlive the applier.
iree_status_t iree_hal_hw_emulator_applier_create(
    hw_device_t* hw_device, iree_allocator_t host_allocator,
    iree_hal_command_buffer_t** out_command_buffer);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_COMMAND_BUFFER_H_
