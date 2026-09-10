// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/executable.h"

#include <string.h>

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_executable_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_hw_emulator_executable_t {
  iree_hal_resource_t resource;
  iree_allocator_t host_allocator;

  // Owned copy of the export table (see executable.h). NULL/0 if empty.
  iree_hal_hw_emulator_executable_export_t* exports;
  iree_host_size_t export_count;
} iree_hal_hw_emulator_executable_t;

static const iree_hal_executable_vtable_t iree_hal_hw_emulator_executable_vtable;

static iree_hal_hw_emulator_executable_t* iree_hal_hw_emulator_executable_cast(
    iree_hal_executable_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_executable_vtable);
  return (iree_hal_hw_emulator_executable_t*)base_value;
}

// Returns |entry|'s kernel_name as a string view, validating that it is
// NUL-terminated within the field's fixed capacity.
static iree_status_t iree_hal_hw_emulator_export_kernel_name(
    const iree_hal_hw_emulator_executable_export_t* entry,
    iree_string_view_t* out_name) {
  const void* nul = memchr(entry->kernel_name, '\0',
                           IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY);
  if (!nul) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "hw_emulator executable export kernel_name is not NUL-terminated "
        "within %d bytes",
        IREE_HAL_HW_EMULATOR_KERNEL_NAME_CAPACITY);
  }
  *out_name = iree_make_cstring_view(entry->kernel_name);
  return iree_ok_status();
}

// Returns |entry|'s export_name as a string view, validating that it is
// NUL-terminated within the field's fixed capacity. See executable.h for why
// this is distinct from kernel_name.
static iree_status_t iree_hal_hw_emulator_export_name(
    const iree_hal_hw_emulator_executable_export_t* entry,
    iree_string_view_t* out_name) {
  const void* nul = memchr(entry->export_name, '\0',
                           IREE_HAL_HW_EMULATOR_EXPORT_NAME_CAPACITY);
  if (!nul) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "hw_emulator executable export_name is not NUL-terminated within %d "
        "bytes",
        IREE_HAL_HW_EMULATOR_EXPORT_NAME_CAPACITY);
  }
  *out_name = iree_make_cstring_view(entry->export_name);
  return iree_ok_status();
}

iree_status_t iree_hal_hw_emulator_executable_create(
    const iree_hal_executable_params_t* executable_params,
    iree_allocator_t host_allocator, iree_hal_executable_t** out_executable) {
  IREE_ASSERT_ARGUMENT(executable_params);
  IREE_ASSERT_ARGUMENT(out_executable);
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_executable = NULL;

  // Allocate storage for the executable and its associated data structures.
  iree_hal_hw_emulator_executable_t* executable = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_allocator_malloc(host_allocator, sizeof(*executable),
                                (void**)&executable));
  iree_hal_resource_initialize(&iree_hal_hw_emulator_executable_vtable,
                               &executable->resource);
  executable->host_allocator = host_allocator;
  executable->exports = NULL;
  executable->export_count = 0;

  // executable_data is expected to be a raw array of
  // iree_hal_hw_emulator_executable_export_t (see executable.h for why: no
  // compiler plugin exists yet to emit a real serialized format). It is not
  // guaranteed to outlive this call, so we copy it.
  iree_status_t status = iree_ok_status();
  const iree_host_size_t export_stride =
      sizeof(iree_hal_hw_emulator_executable_export_t);
  if (executable_params->executable_data.data_length % export_stride != 0) {
    status = iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "executable_data is not a whole number of hw_emulator exports");
  } else {
    executable->export_count =
        executable_params->executable_data.data_length / export_stride;
    if (executable->export_count > 0) {
      status = iree_allocator_malloc(
          host_allocator, executable_params->executable_data.data_length,
          (void**)&executable->exports);
      if (iree_status_is_ok(status)) {
        memcpy(executable->exports, executable_params->executable_data.data,
               executable_params->executable_data.data_length);
      }
    }
  }

  // Verify every export names a real, NUL-terminated, registered kernel now
  // rather than deferring the failure to first dispatch.
  for (iree_host_size_t i = 0;
       iree_status_is_ok(status) && i < executable->export_count; ++i) {
    iree_string_view_t kernel_name;
    status = iree_hal_hw_emulator_export_kernel_name(&executable->exports[i],
                                                      &kernel_name);
    if (iree_status_is_ok(status)) {
      const iree_hal_hw_emulator_kernel_registration_t* registration = NULL;
      status = iree_hal_hw_emulator_kernel_registry_lookup(kernel_name,
                                                            &registration);
    }
  }

  if (iree_status_is_ok(status)) {
    *out_executable = (iree_hal_executable_t*)executable;
  } else {
    iree_hal_executable_destroy((iree_hal_executable_t*)executable);
  }
  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_hw_emulator_executable_destroy(
    iree_hal_executable_t* base_executable) {
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  iree_allocator_t host_allocator = executable->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_allocator_free(host_allocator, executable->exports);
  iree_allocator_free(host_allocator, executable);

  IREE_TRACE_ZONE_END(z0);
}

static iree_host_size_t iree_hal_hw_emulator_executable_export_count(
    iree_hal_executable_t* base_executable) {
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  return executable->export_count;
}

iree_status_t iree_hal_hw_emulator_executable_lookup_export(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t export_ordinal,
    const iree_hal_hw_emulator_kernel_registration_t** out_registration) {
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  *out_registration = NULL;
  if (!iree_hal_executable_function_is_index_in_range(
          export_ordinal, executable->export_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "export ordinal %" PRIu64
                            " out of range (count: %" PRIhsz ")",
                            export_ordinal.value, executable->export_count);
  }
  const iree_hal_hw_emulator_executable_export_t* entry =
      &executable->exports[iree_hal_executable_function_index(export_ordinal)];
  iree_string_view_t kernel_name;
  IREE_RETURN_IF_ERROR(
      iree_hal_hw_emulator_export_kernel_name(entry, &kernel_name));
  return iree_hal_hw_emulator_kernel_registry_lookup(kernel_name,
                                                      out_registration);
}

iree_status_t iree_hal_hw_emulator_executable_lookup_export_params(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t export_ordinal,
    const uint8_t** out_kernel_params) {
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  *out_kernel_params = NULL;
  if (!iree_hal_executable_function_is_index_in_range(
          export_ordinal, executable->export_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "export ordinal %" PRIu64
                            " out of range (count: %" PRIhsz ")",
                            export_ordinal.value, executable->export_count);
  }
  *out_kernel_params =
      executable->exports[iree_hal_executable_function_index(export_ordinal)]
          .kernel_params;
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_executable_export_info(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t export_ordinal,
    iree_hal_executable_function_info_t* out_info) {
  memset(out_info, 0, sizeof(*out_info));
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  if (!iree_hal_executable_function_is_index_in_range(
          export_ordinal, executable->export_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "export ordinal %" PRIu64
                            " out of range (count: %" PRIhsz ")",
                            export_ordinal.value, executable->export_count);
  }
  iree_string_view_t export_name;
  IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_export_name(
      &executable->exports[iree_hal_executable_function_index(export_ordinal)],
      &export_name));
  const iree_hal_hw_emulator_kernel_registration_t* registration = NULL;
  IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_executable_lookup_export(
      base_executable, export_ordinal, &registration));
  out_info->name = export_name;
  out_info->binding_count =
      (uint16_t)(registration->num_inputs + registration->num_outputs);
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_executable_export_parameters(
    iree_hal_executable_t* base_executable,
    iree_hal_executable_function_t export_ordinal, iree_host_size_t capacity,
    iree_hal_executable_function_parameter_t* out_parameters) {
  // No parameter reflection: bindings are inputs-then-outputs by convention
  // (see executable.h) and there's nothing more descriptive to report yet.
  (void)base_executable;
  (void)export_ordinal;
  (void)capacity;
  (void)out_parameters;
  return iree_ok_status();
}

static iree_status_t iree_hal_hw_emulator_executable_lookup_export_by_name(
    iree_hal_executable_t* base_executable, iree_string_view_t name,
    iree_hal_executable_function_t* out_export_ordinal) {
  // Matched against export_name (the compiler-assigned
  // `hal.executable.export` symbol, e.g. "on_hw_dispatch_0_matmul_2x2x2_f32"
  // - NOT kernel_name, e.g. "matmul"). `hal.executable.lookup.function`
  // always resolves indirectly by this symbol at runtime, even though it's
  // known statically at compile time - confirmed empirically 2026-09-10 via
  // `iree-run-module`, which previously failed here with "no export named
  // 'on_hw_dispatch_0_matmul_2x2x2_f32'" while comparing against kernel_name
  // ("matmul") instead. See executable.h for the full writeup.
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  for (iree_host_size_t i = 0; i < executable->export_count; ++i) {
    iree_string_view_t export_name;
    IREE_RETURN_IF_ERROR(iree_hal_hw_emulator_export_name(
        &executable->exports[i], &export_name));
    if (iree_string_view_equal(export_name, name)) {
      *out_export_ordinal = iree_hal_executable_function_from_value((uint64_t)i);
      return iree_ok_status();
    }
  }
  return iree_make_status(IREE_STATUS_NOT_FOUND,
                          "no export named '%.*s'", (int)name.size, name.data);
}

static iree_status_t iree_hal_hw_emulator_executable_lookup_global_by_name(
    iree_hal_executable_t* base_executable, iree_string_view_t name,
    iree_hal_queue_affinity_t queue_affinity, iree_hal_buffer_t** out_buffer) {
  iree_hal_hw_emulator_executable_t* executable =
      iree_hal_hw_emulator_executable_cast(base_executable);
  (void)executable;
  (void)name;
  (void)queue_affinity;
  *out_buffer = NULL;
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "global lookup not implemented");
}

static const iree_hal_executable_vtable_t iree_hal_hw_emulator_executable_vtable = {
    .destroy = iree_hal_hw_emulator_executable_destroy,
    .function_count = iree_hal_hw_emulator_executable_export_count,
    .function_info = iree_hal_hw_emulator_executable_export_info,
    .function_parameters = iree_hal_hw_emulator_executable_export_parameters,
    .lookup_function_by_name = iree_hal_hw_emulator_executable_lookup_export_by_name,
    .lookup_global_by_name = iree_hal_hw_emulator_executable_lookup_global_by_name,
};
