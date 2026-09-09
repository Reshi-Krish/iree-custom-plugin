// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "hw_runtime_plugin/channel.h"

//===----------------------------------------------------------------------===//
// iree_hal_hw_emulator_channel_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_hw_emulator_channel_t {
  iree_hal_resource_t resource;
  iree_allocator_t host_allocator;

  // Parent channel this was split from, if any.
  // This is only used to keep the parent channel live for as long as there are
  // any split channels live (including transitive splits).
  iree_hal_channel_t* parent_channel;

  // hw_emulator has no collective communication fabric (there is exactly one
  // emulated device), so the only channel it can ever honestly represent is
  // the trivial single-participant group: rank 0 of 1.
  int32_t rank;
  int32_t count;
} iree_hal_hw_emulator_channel_t;

static const iree_hal_channel_vtable_t iree_hal_hw_emulator_channel_vtable;

static iree_hal_hw_emulator_channel_t* iree_hal_hw_emulator_channel_cast(
    iree_hal_channel_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_channel_vtable);
  return (iree_hal_hw_emulator_channel_t*)base_value;
}

static const iree_hal_hw_emulator_channel_t* iree_hal_hw_emulator_channel_const_cast(
    const iree_hal_channel_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_hw_emulator_channel_vtable);
  return (const iree_hal_hw_emulator_channel_t*)base_value;
}

iree_status_t iree_hal_hw_emulator_channel_create(iree_hal_channel_params_t params,
                                           iree_allocator_t host_allocator,
                                           iree_hal_channel_t** out_channel) {
  IREE_ASSERT_ARGUMENT(out_channel);
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_channel = NULL;

  iree_hal_hw_emulator_channel_t* channel = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_allocator_malloc(host_allocator, sizeof(*channel),
                                (void**)&channel));
  iree_hal_resource_initialize(&iree_hal_hw_emulator_channel_vtable,
                               &channel->resource);
  channel->host_allocator = host_allocator;
  channel->parent_channel = NULL;

  const int32_t rank =
      params.rank == IREE_HAL_CHANNEL_RANK_DEFAULT ? 0 : params.rank;
  const int32_t count =
      params.count == IREE_HAL_CHANNEL_COUNT_DEFAULT ? 1 : params.count;
  iree_status_t status = iree_ok_status();
  if (rank != 0 || count != 1) {
    // There's no real communication fabric behind this driver to honor a
    // request for an actual multi-participant group.
    status = iree_make_status(
        IREE_STATUS_UNAVAILABLE,
        "hw_emulator has no collective communication fabric; only the "
        "trivial single-participant channel (rank 0 of 1) is supported, "
        "requested rank %d of %d",
        rank, count);
  }
  channel->rank = rank;
  channel->count = count;

  if (iree_status_is_ok(status)) {
    *out_channel = (iree_hal_channel_t*)channel;
  } else {
    iree_hal_channel_release((iree_hal_channel_t*)channel);
  }
  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_hw_emulator_channel_destroy(iree_hal_channel_t* base_channel) {
  iree_hal_hw_emulator_channel_t* channel = iree_hal_hw_emulator_channel_cast(base_channel);
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_allocator_t host_allocator = channel->host_allocator;

  // TODO(null): destroy any implementation resources.

  iree_hal_channel_release(channel->parent_channel);
  iree_allocator_free(host_allocator, channel);

  IREE_TRACE_ZONE_END(z0);
}

static iree_status_t iree_hal_hw_emulator_channel_split(
    iree_hal_channel_t* base_channel, int32_t color, int32_t key,
    iree_hal_channel_flags_t flags, iree_hal_channel_t** out_split_channel) {
  iree_hal_hw_emulator_channel_t* channel = iree_hal_hw_emulator_channel_cast(base_channel);
  *out_split_channel = NULL;

  if (color == IREE_HAL_CHANNEL_NO_COLOR) {
    // Not a participant in the new group.
    return iree_ok_status();
  }

  // With only one participant to begin with, any split (regardless of key)
  // trivially yields another single-participant channel.
  iree_hal_hw_emulator_channel_t* split_channel = NULL;
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc(channel->host_allocator, sizeof(*split_channel),
                            (void**)&split_channel));
  iree_hal_resource_initialize(&iree_hal_hw_emulator_channel_vtable,
                               &split_channel->resource);
  split_channel->host_allocator = channel->host_allocator;
  split_channel->parent_channel = base_channel;
  iree_hal_channel_retain(base_channel);
  split_channel->rank = 0;
  split_channel->count = 1;

  *out_split_channel = (iree_hal_channel_t*)split_channel;
  return iree_ok_status();
}

static void iree_hal_hw_emulator_channel_query_rank_and_count(
    const iree_hal_channel_t* base_channel, int32_t* out_rank,
    int32_t* out_count) {
  const iree_hal_hw_emulator_channel_t* channel =
      iree_hal_hw_emulator_channel_const_cast(base_channel);
  *out_rank = channel->rank;
  *out_count = channel->count;
}

static const iree_hal_channel_vtable_t iree_hal_hw_emulator_channel_vtable = {
    .destroy = iree_hal_hw_emulator_channel_destroy,
    .split = iree_hal_hw_emulator_channel_split,
    .query_rank_and_count = iree_hal_hw_emulator_channel_query_rank_and_count,
};
