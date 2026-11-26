cmake_minimum_required(VERSION 3.22)
project(learn_vulkan LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()
if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /Zc:preprocessor")
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
enable_testing()

include(cmake/rhi_host.cmake)
if(NOT LEARN_VULKAN_BUILD_DEVICE)
    return()
endif()

configure_file(src/config.h.in "${PROJECT_BINARY_DIR}/generated/config.h")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/my_vulkan/*.cpp" "src/utility/*.cpp" "src/test/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "src/my_vulkan/*.h" "src/utility/*.h" "src/test/*.h")
list(REMOVE_ITEM SOURCES ${RHI_HOST_SOURCES})
add_executable(learn_vulkan ${SOURCES} ${HEADERS} src/common.h)
source_group(TREE "${PROJECT_SOURCE_DIR}/src" FILES ${SOURCES} ${HEADERS})
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT learn_vulkan)

if(MSVC)
    target_compile_options(learn_vulkan PRIVATE /EHsc /utf-8)
endif()
add_test(NAME learn_vulkan_tests COMMAND learn_vulkan)
option(LEARN_VULKAN_GPU_TESTS "Enable headless Vulkan 1.3 shader binding smoke tests" OFF)
if(EXISTS "${PROJECT_SOURCE_DIR}/src/test/shader_binding_tests.cpp")
    target_compile_definitions(learn_vulkan PRIVATE LEARN_VULKAN_HAS_SHADER_BINDING_TESTS=1)
    if(LEARN_VULKAN_GPU_TESTS)
        add_test(NAME learn_vulkan_gpu_tests COMMAND learn_vulkan)
        set_tests_properties(learn_vulkan_gpu_tests PROPERTIES
            ENVIRONMENT "LEARN_VULKAN_GPU_TESTS=1" RUN_SERIAL TRUE)
    endif()
elseif(LEARN_VULKAN_GPU_TESTS)
    message(FATAL_ERROR "LEARN_VULKAN_GPU_TESTS requires src/test/shader_binding_tests.cpp")
endif()

add_subdirectory(external)
add_subdirectory(res)
set_target_properties(ExternalLibs PROPERTIES FOLDER "ThirdParty")
target_include_directories(learn_vulkan PRIVATE
    "${PROJECT_BINARY_DIR}/generated" src src/my_vulkan src/my_vulkan/command
    src/my_vulkan/pipeline src/my_vulkan/resource src/utility "${SHADERC_INCLUDE_DIR}")
target_link_libraries(learn_vulkan PRIVATE ExternalLibs rhi_host "${SHADERC_LIBRARY}")
if(SHADERC_RUNTIME_LIBRARY)
    add_custom_command(TARGET learn_vulkan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADERC_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:learn_vulkan>" VERBATIM)
endif()
