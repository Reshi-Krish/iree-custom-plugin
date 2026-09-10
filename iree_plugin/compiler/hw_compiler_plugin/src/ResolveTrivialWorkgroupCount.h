// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_PLUGIN_COMPILER_HW_COMPILER_PLUGIN_RESOLVE_TRIVIAL_WORKGROUP_COUNT_H_
#define IREE_PLUGIN_COMPILER_HW_COMPILER_PLUGIN_RESOLVE_TRIVIAL_WORKGROUP_COUNT_H_

#include <memory>

#include "mlir/Pass/Pass.h"

namespace mlir::iree_compiler::IREE::HAL {

std::unique_ptr<Pass> createResolveTrivialWorkgroupCountPass();

} // namespace mlir::iree_compiler::IREE::HAL

#endif // IREE_PLUGIN_COMPILER_HW_COMPILER_PLUGIN_RESOLVE_TRIVIAL_WORKGROUP_COUNT_H_
