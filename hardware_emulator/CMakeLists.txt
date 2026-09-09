add_library(hardware_emulator STATIC
  include/hardwareEmulator.h
  src/hardwareEmulator.c
)

target_include_directories(hardware_emulator PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(hardware_emulator PUBLIC Threads::Threads)
