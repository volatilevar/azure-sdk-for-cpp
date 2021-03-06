# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

# Configure CMake project.
cmake_minimum_required (VERSION 3.13)
project(azure-security-keyvault-keys-performance LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(
  AZURE_KEYVAULT_KEY_PERF_TEST_HEADER
  inc/azure/keyvault/keys/test/performance/get_key.hpp
)

set(
  AZURE_KEYVAULT_KEY_PERF_TEST_SOURCE
    src/main.cpp
)

# Name the binary to be created.
add_executable (
  azure-security-keyvault-keys-performance
     ${AZURE_KEYVAULT_KEY_PERF_TEST_HEADER} ${AZURE_KEYVAULT_KEY_PERF_TEST_SOURCE}
)

# Include the headers from the project.
target_include_directories(
  azure-security-keyvault-keys-performance
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
)

# link the `azure-performance-stress` lib together with any other library which will be used for the tests. 
target_link_libraries(azure-security-keyvault-keys-performance PRIVATE azure-identity azure-security-keyvault-keys azure-performance-stress)
# Make sure the project will appear in the test folder for Visual Studio CMake view
set_target_properties(azure-security-keyvault-keys-performance PROPERTIES FOLDER "Tests/Keyvault")
