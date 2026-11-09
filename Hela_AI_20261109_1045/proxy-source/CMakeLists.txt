cmake_minimum_required(VERSION 3.24)
project(d3d12on11 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "d3d12on11 is a Windows-only proxy.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "d3d12on11 currently supports Windows x64 only.")
endif()

add_library(d3d12 SHARED
  src/device.cpp
  src/backbuffer.cpp
  src/descriptors.cpp
  src/commands.cpp
  src/exports.cpp
  src/d3d12.def
  src/pipeline.cpp
  src/resource.cpp
  src/rootsig.cpp
  src/util.cpp
)

target_compile_features(d3d12 PRIVATE cxx_std_17)
target_compile_definitions(d3d12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(d3d12 PRIVATE src generated)
target_link_libraries(d3d12 PRIVATE d3d11 dxgi dxguid)
set_target_properties(d3d12 PROPERTIES OUTPUT_NAME d3d12)

add_library(dxgi_proxy SHARED src/dxgi_proxy.cpp src/dxgi_proxy.def)
target_compile_features(dxgi_proxy PRIVATE cxx_std_17)
target_compile_definitions(dxgi_proxy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX D3D12ON11_BRIDGE_IMPORT)
target_include_directories(dxgi_proxy PRIVATE src generated)
target_link_libraries(dxgi_proxy PRIVATE d3d12 dxgi dxguid)
set_target_properties(dxgi_proxy PROPERTIES OUTPUT_NAME dxgi)

add_library(nvapi_trace SHARED EXCLUDE_FROM_ALL
  tools/nvapi_trace.cpp
  tools/nvapi_trace.def
)
target_compile_features(nvapi_trace PRIVATE cxx_std_17)
target_compile_definitions(nvapi_trace PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(nvapi_trace PROPERTIES OUTPUT_NAME nvapi64)

include(CTest)
if(BUILD_TESTING)
  add_executable(d3d12on11_smoke tests/smoke.cpp)
  target_compile_features(d3d12on11_smoke PRIVATE cxx_std_17)
  target_compile_definitions(d3d12on11_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(d3d12on11_smoke PRIVATE d3d12 d3dcompiler)
  add_test(NAME d3d12on11_smoke COMMAND d3d12on11_smoke)
  set_tests_properties(d3d12on11_smoke PROPERTIES ENVIRONMENT "D3D12ON11_SHADER_MODEL=6_7")

  add_executable(dxgi_proxy_smoke tests/dxgi_smoke.cpp)
  target_compile_features(dxgi_proxy_smoke PRIVATE cxx_std_17)
  target_compile_definitions(dxgi_proxy_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(dxgi_proxy_smoke PRIVATE dxgi_proxy d3d12)
  add_test(NAME dxgi_proxy_smoke COMMAND dxgi_proxy_smoke)

  add_executable(dxgi_swapchain_smoke tests/dxgi_swapchain_smoke.cpp)
  target_compile_features(dxgi_swapchain_smoke PRIVATE cxx_std_17)
  target_compile_definitions(dxgi_swapchain_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(dxgi_swapchain_smoke PRIVATE dxgi_proxy d3d12 d3dcompiler)
  add_test(NAME dxgi_swapchain_smoke COMMAND dxgi_swapchain_smoke)

  add_executable(d3d12on11_shader_override_smoke tests/shader_override_smoke.cpp)
  target_compile_features(d3d12on11_shader_override_smoke PRIVATE cxx_std_17)
  target_compile_definitions(d3d12on11_shader_override_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(d3d12on11_shader_override_smoke PRIVATE d3d12)

  add_executable(d3d12on11_queue_clock_smoke tests/queue_clock_smoke.cpp)
  target_compile_features(d3d12on11_queue_clock_smoke PRIVATE cxx_std_17)
  target_compile_definitions(d3d12on11_queue_clock_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(d3d12on11_queue_clock_smoke PRIVATE d3d12)
  add_test(NAME d3d12on11_queue_clock_smoke COMMAND d3d12on11_queue_clock_smoke)
endif()
