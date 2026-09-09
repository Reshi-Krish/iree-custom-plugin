// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_ALLOCATOR_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_ALLOCATOR_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_allocator_t
//===----------------------------------------------------------------------===//

// Creates a hw_emulator buffer allocator used for persistent allocations.
// |parent_device| and |hw_device| are borrowed and must outlive the
// allocator (the owning device releases the allocator before it destroys
// itself or the underlying hw_device).
iree_status_t iree_hal_hw_emulator_allocator_create(
    iree_hal_device_t* parent_device, hw_device_t* hw_device,
    iree_allocator_t host_allocator, iree_hal_allocator_t** out_allocator);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_ALLOCATOR_H_
