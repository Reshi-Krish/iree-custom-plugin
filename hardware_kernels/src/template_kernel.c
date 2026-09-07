#include "template_kernel.h"

#include <stdio.h>

hw_status_t template_kernel(void* const* inputs, size_t num_inputs,
                             void* const* outputs, size_t num_outputs,
                             void* params) {
  (void)inputs;
  (void)num_inputs;
  (void)outputs;
  (void)num_outputs;
  (void)params;

  printf("Hello, world! (template_kernel executing on the hardware emulator)\n");

  return hw_status_ok();
}
