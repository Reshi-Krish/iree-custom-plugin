// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Minimal end-to-end smoke test for the hw_emulator allocator/buffer path:
// create a driver + device, allocate a buffer, write a pattern through a
// host mapping, and read it back through a second mapping to confirm the
// hw_allocate()/hw_buffer_map() wiring actually works (not just compiles).
//
// This intentionally does not use gtest: it mirrors the plain-C
// executable_library_test.c pattern elsewhere in the IREE tree.

#include <stdio.h>
#include <string.h>

#include "hw_runtime_plugin/api.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/base/api.h"
#include "iree/hal/api.h"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  iree_allocator_t host_allocator = iree_allocator_system();

  // The device requires a proactor pool for async I/O even though this
  // hw_emulator skeleton has no real async submit path yet.
  iree_async_proactor_pool_t* proactor_pool = NULL;
  IREE_CHECK_OK(iree_async_proactor_pool_create(
      /*node_count=*/1, /*node_ids=*/NULL,
      iree_async_proactor_pool_options_default(), host_allocator,
      &proactor_pool));

  iree_hal_hw_emulator_driver_options_t driver_options;
  iree_hal_hw_emulator_driver_options_initialize(&driver_options);
  iree_hal_driver_t* driver = NULL;
  IREE_CHECK_OK(iree_hal_hw_emulator_driver_create(
      IREE_SV("hw_emulator"), &driver_options, host_allocator, &driver));

  iree_hal_device_create_params_t create_params =
      iree_hal_device_create_params_default();
  create_params.proactor_pool = proactor_pool;
  iree_hal_device_t* device = NULL;
  IREE_CHECK_OK(iree_hal_driver_create_default_device(
      driver, &create_params, host_allocator, &device));
  iree_async_proactor_pool_release(proactor_pool);

  iree_hal_allocator_t* allocator = iree_hal_device_allocator(device);

  const iree_device_size_t kBufferSize = 256;
  iree_hal_buffer_params_t buffer_params;
  memset(&buffer_params, 0, sizeof(buffer_params));
  iree_hal_buffer_t* buffer = NULL;
  IREE_CHECK_OK(iree_hal_allocator_allocate_buffer(
      allocator, buffer_params, kBufferSize, &buffer));

  // Write a recognizable pattern through a host mapping.
  iree_hal_buffer_mapping_t write_mapping;
  IREE_CHECK_OK(iree_hal_buffer_map_range(
      buffer, IREE_HAL_MAPPING_MODE_SCOPED,
      IREE_HAL_MEMORY_ACCESS_WRITE | IREE_HAL_MEMORY_ACCESS_DISCARD,
      /*byte_offset=*/0, kBufferSize, &write_mapping));
  for (iree_device_size_t i = 0; i < kBufferSize; ++i) {
    write_mapping.contents.data[i] = (uint8_t)i;
  }
  IREE_CHECK_OK(iree_hal_buffer_mapping_flush_range(&write_mapping, 0,
                                                    kBufferSize));
  IREE_CHECK_OK(iree_hal_buffer_unmap_range(&write_mapping));

  // Read it back through a fresh mapping and confirm it matches.
  iree_hal_buffer_mapping_t read_mapping;
  IREE_CHECK_OK(iree_hal_buffer_map_range(
      buffer, IREE_HAL_MAPPING_MODE_SCOPED, IREE_HAL_MEMORY_ACCESS_READ,
      /*byte_offset=*/0, kBufferSize, &read_mapping));
  IREE_CHECK_OK(
      iree_hal_buffer_mapping_invalidate_range(&read_mapping, 0, kBufferSize));
  bool all_match = true;
  for (iree_device_size_t i = 0; i < kBufferSize; ++i) {
    if (read_mapping.contents.data[i] != (uint8_t)i) {
      fprintf(stderr, "mismatch at byte %llu: got %u, expected %u\n",
              (unsigned long long)i, read_mapping.contents.data[i],
              (uint8_t)i);
      all_match = false;
      break;
    }
  }
  IREE_CHECK_OK(iree_hal_buffer_unmap_range(&read_mapping));

  iree_hal_buffer_release(buffer);
  iree_hal_device_release(device);
  iree_hal_driver_release(driver);

  if (all_match) {
    fprintf(stdout, "hw_emulator allocator smoke test passed\n");
  }
  return all_match ? 0 : 1;
}
