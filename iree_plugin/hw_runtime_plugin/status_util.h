// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_STATUS_UTIL_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_STATUS_UTIL_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"

// Converts a hw_status_t returned by the hardware_emulator library into an
// iree_status_t, preserving the emulator's message where available.
static inline iree_status_t iree_hal_hw_emulator_status_from_hw(
    hw_status_t hw_status) {
  if (hw_status_is_ok(hw_status)) return iree_ok_status();
  iree_status_code_t code = IREE_STATUS_INTERNAL;
  switch (hw_status.code) {
    case HW_STATUS_INVALID_ARGUMENT:
      code = IREE_STATUS_INVALID_ARGUMENT;
      break;
    case HW_STATUS_OUT_OF_MEMORY:
      code = IREE_STATUS_RESOURCE_EXHAUSTED;
      break;
    case HW_STATUS_OUT_OF_RANGE:
      code = IREE_STATUS_OUT_OF_RANGE;
      break;
    case HW_STATUS_INTERNAL_ERROR:
    default:
      code = IREE_STATUS_INTERNAL;
      break;
  }
  return iree_make_status(
      code, "%s", hw_status.message ? hw_status.message : "hw_emulator error");
}

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_STATUS_UTIL_H_
