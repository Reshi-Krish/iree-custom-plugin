// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_DEVICE_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_DEVICE_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_device_t
//===----------------------------------------------------------------------===//

// Returns the device's internal "applier" command buffer (see
// command_buffer.h) used to execute recorded command buffers against
// hw_device. There is no public HAL API for this - queue_execute uses it
// internally (once implemented) via iree_hal_deferred_command_buffer_apply.
// Exposed only so tests can exercise record -> apply without a real
// queue_execute yet.
iree_hal_command_buffer_t* iree_hal_hw_emulator_device_debug_applier(
    iree_hal_device_t* base_device);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_DEVICE_H_
