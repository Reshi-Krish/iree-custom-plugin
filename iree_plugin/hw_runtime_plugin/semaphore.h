// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_SEMAPHORE_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_SEMAPHORE_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_semaphore_t
//===----------------------------------------------------------------------===//

// Creates a hw_emulator semaphore used for ordering queue operations and
// synchronizing between host/device and device/device.
// |proactor| is borrowed from the device's proactor pool and must outlive the
// semaphore. |hw_device| is borrowed from the owning device: wait() polls its
// hw_query_execution_status() flag as a stand-in for a real device's
// completion/doorbell register, in addition to the normal host-signaled path.
iree_status_t iree_hal_hw_emulator_semaphore_create(
    iree_async_proactor_t* proactor, iree_hal_queue_affinity_t queue_affinity,
    uint64_t initial_value, iree_hal_semaphore_flags_t flags,
    hw_device_t* hw_device, iree_allocator_t host_allocator,
    iree_hal_semaphore_t** out_semaphore);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_SEMAPHORE_H_
