cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_mesh_button_node)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../common/include)
target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_LIST_DIR}/../../common/src/app_led.c
  ${CMAKE_CURRENT_LIST_DIR}/../../common/src/app_mesh.c
  ${CMAKE_CURRENT_LIST_DIR}/../../common/src/app_node.c
)