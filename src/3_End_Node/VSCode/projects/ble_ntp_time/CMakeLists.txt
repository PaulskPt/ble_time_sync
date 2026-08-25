
cmake_minimum_required(VERSION 3.20.0)

# Import the core Zephyr build architecture standard hook definitions
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ble_ntp_time)

# Link your source application tracker files
target_sources(app PRIVATE src/main.c)
