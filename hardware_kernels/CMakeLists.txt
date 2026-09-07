add_library(hardware_kernels STATIC
  include/template_kernel.h
  include/matmul_kernel.h
  src/template_kernel.c
  src/matmul_kernel.c
)

target_include_directories(hardware_kernels PUBLIC include)
target_link_libraries(hardware_kernels PUBLIC hardware_emulator)
