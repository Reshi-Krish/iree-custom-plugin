// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_API_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_API_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_device_t
//===----------------------------------------------------------------------===//

// Parameters configuring an iree_hal_hw_emulator_device_t.
// Must be initialized with iree_hal_hw_emulator_device_options_initialize prior to
// use.
typedef struct iree_hal_hw_emulator_device_options_t {
  // Total size, in bytes, of the hw_emulator's single emulated memory pool.
  // All device allocations are sub-allocated from this pool by hw_allocate();
  // the pool does not grow once the device is created.
  iree_device_size_t memory_pool_size;
} iree_hal_hw_emulator_device_options_t;

// Initializes |out_options| to default values.
IREE_API_EXPORT void iree_hal_hw_emulator_device_options_initialize(
    iree_hal_hw_emulator_device_options_t* out_options);

// Creates a hw_emulator HAL device with the given |options|.
//
// The provided |identifier| will be used by programs to distinguish the device
// type from other HAL implementations. If compiling programs with the IREE
// compiler this must match the value used by `IREE::HAL::TargetDevice`.
//
// |out_device| must be released by the caller (see iree_hal_device_release).
IREE_API_EXPORT iree_status_t iree_hal_hw_emulator_device_create(
    iree_string_view_t identifier,
    const iree_hal_hw_emulator_device_options_t* options,
    const iree_hal_device_create_params_t* create_params,
    iree_allocator_t host_allocator, iree_hal_device_t** out_device);

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_driver_t
//===----------------------------------------------------------------------===//

// Parameters for configuring an iree_hal_hw_emulator_driver_t.
// Must be initialized with iree_hal_hw_emulator_driver_options_initialize prior to
// use.
typedef struct iree_hal_hw_emulator_driver_options_t {
  // TODO(null): options for initializing the driver such as library search
  // paths, version min/max, etc.

  // Default device options when none are provided during device creation.
  iree_hal_hw_emulator_device_options_t default_device_options;
} iree_hal_hw_emulator_driver_options_t;

// Initializes the given |out_options| with default driver creation options.
IREE_API_EXPORT void iree_hal_hw_emulator_driver_options_initialize(
    iree_hal_hw_emulator_driver_options_t* out_options);

// Creates a hw_emulator HAL driver with the given |options|, from which hw_emulator
// devices can be enumerated and created with specific parameters.
//
// The provided |identifier| will be used by programs to distinguish the device
// type from other HAL implementations. If compiling programs with the IREE
// compiler this must match the value used by IREE::HAL::TargetDevice.
//
// |out_driver| must be released by the caller (see iree_hal_driver_release).
IREE_API_EXPORT iree_status_t iree_hal_hw_emulator_driver_create(
    iree_string_view_t identifier,
    const iree_hal_hw_emulator_driver_options_t* options,
    iree_allocator_t host_allocator, iree_hal_driver_t** out_driver);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_API_H_
