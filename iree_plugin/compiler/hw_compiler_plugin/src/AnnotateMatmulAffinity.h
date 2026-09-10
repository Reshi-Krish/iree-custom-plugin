// Copyright 2024 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef HW_COMPILER_PLUGIN_ANNOTATE_MATMUL_AFFINITY_H_
#define HW_COMPILER_PLUGIN_ANNOTATE_MATMUL_AFFINITY_H_

#include <memory>

#include "mlir/Pass/Pass.h"

namespace mlir::iree_compiler::IREE::HAL {

// Assigns hw_emulator device affinity to functions containing a
// linalg.matmul. See AnnotateMatmulAffinity.cpp for the full design
// rationale and known limitations.
std::unique_ptr<Pass> createAnnotateMatmulAffinityPass();

} // namespace mlir::iree_compiler::IREE::HAL

#endif // HW_COMPILER_PLUGIN_ANNOTATE_MATMUL_AFFINITY_H_
