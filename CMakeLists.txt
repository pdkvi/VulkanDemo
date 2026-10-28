cmake_minimum_required(VERSION 3.30)
project(VulkanDemo LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

add_subdirectory("Sources")
