#ifndef HARDWARE_KERNELS_TEMPLATE_KERNEL_H_
#define HARDWARE_KERNELS_TEMPLATE_KERNEL_H_

#include "hardwareEmulator.h"

#ifdef __cplusplus
extern "C" {
#endif

// A no-op kernel that ignores all buffers and params and just prints a
// message. This is the minimal shape every kernel must conform to in order
// to be passed as the `kernel` argument to hw_execute() -- see
// hardware_kernels/README.md for the full ABI contract.
hw_status_t template_kernel(void* const* inputs, size_t num_inputs,
                             void* const* outputs, size_t num_outputs,
                             void* params);

#ifdef __cplusplus
}
#endif

#endif  // HARDWARE_KERNELS_TEMPLATE_KERNEL_H_
