// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_
#define IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_

#include "hardwareEmulator.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_executable_t
//===----------------------------------------------------------------------===//

// A single named export in a hw_emulator executable: a plain kernel function
// pointer plus how its bindings split between inputs and outputs (bindings
// are ordered inputs-then-outputs by convention).
//
// This is a deliberately simple placeholder for what a compiler target would
// normally emit as a serialized, verifiable format (e.g. Flatbuffers, as
// upstream IREE HALs do) - there is no hw_emulator compiler plugin yet, so
// executable_data is expected to be a raw array of these structs, built
// directly by whoever is constructing the executable (test code today, the
// future compiler plugin later). Names must have static/program lifetime;
// see iree_hal_hw_emulator_executable_create for why (it only shallow-copies
// the table).
typedef struct iree_hal_hw_emulator_executable_export_t {
  iree_string_view_t name;
  hw_kernel_fn_t kernel;
  iree_host_size_t num_inputs;
  iree_host_size_t num_outputs;
} iree_hal_hw_emulator_executable_export_t;

#define IREE_HAL_HW_EMULATOR_EXECUTABLE_FORMAT "{hw_emulator-executable}"

// Creates a hw_emulator executable from a binary in memory. Each executable may
// contain multiple entry points and be composed of several modules presented to
// the HAL as a single instance. See iree_hal_executable_params_t for more
// information about the lifetime of the resources referenced within.
iree_status_t iree_hal_hw_emulator_executable_create(
    const iree_hal_executable_params_t* executable_params,
    iree_allocator_t host_allocator, iree_hal_executable_t** out_executable);

// Returns the export table entry for |export_ordinal|, or
// IREE_STATUS_OUT_OF_RANGE if it does not exist. Used by the applier command
// buffer (command_buffer.c) to resolve a dispatch to a kernel to run.
iree_status_t iree_hal_hw_emulator_executable_lookup_export(
    iree_hal_executable_t* executable,
    iree_hal_executable_function_t export_ordinal,
    const iree_hal_hw_emulator_executable_export_t** out_export);

#endif  // IREE_HAL_DRIVERS_HW_EMULATOR_EXECUTABLE_H_
