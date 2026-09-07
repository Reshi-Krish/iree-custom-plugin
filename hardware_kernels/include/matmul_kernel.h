#ifndef HARDWARE_KERNELS_MATMUL_KERNEL_H_
#define HARDWARE_KERNELS_MATMUL_KERNEL_H_

#include <stdint.h>

#include "hardwareEmulator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shape descriptor passed as `params` to matmul_kernel().
typedef struct hw_matmul_params {
  int64_t m;
  int64_t n;
  int64_t k;
} hw_matmul_params_t;

// Computes C[m,n] = A[m,k] * B[k,n] over row-major, contiguous, float32
// buffers.
//
//   inputs  = { A, B }
//   outputs = { C }
//   params  = &(hw_matmul_params_t){ .m = m, .n = n, .k = k }
//
// A, B, and C must point to buffers of at least m*k, k*n, and m*n floats
// respectively -- this is not checked, see hardware_kernels/README.md.
hw_status_t matmul_kernel(void* const* inputs, size_t num_inputs,
                           void* const* outputs, size_t num_outputs,
                           void* params);

#ifdef __cplusplus
}
#endif

#endif  // HARDWARE_KERNELS_MATMUL_KERNEL_H_
