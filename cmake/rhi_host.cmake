option(LEARN_VULKAN_BUILD_DEVICE "Build the Vulkan runtime and device regression tests" ON)

find_path(SHADERC_INCLUDE_DIR NAMES shaderc/shaderc.hpp
    HINTS "$ENV{VK_SDK_PATH}/Include" "$ENV{VULKAN_SDK}/Include")
find_library(SHADERC_SHARED_LIBRARY NAMES shaderc_shared
    HINTS "$ENV{VK_SDK_PATH}/Lib" "$ENV{VULKAN_SDK}/Lib")
find_library(SHADERC_COMBINED_LIBRARY NAMES shaderc_combined
    HINTS "$ENV{VK_SDK_PATH}/Lib" "$ENV{VULKAN_SDK}/Lib")
if(SHADERC_SHARED_LIBRARY)
    set(SHADERC_LIBRARY "${SHADERC_SHARED_LIBRARY}")
    find_file(SHADERC_RUNTIME_LIBRARY NAMES shaderc_shared.dll shaderc_sharedd.dll
        HINTS "$ENV{VK_SDK_PATH}/Bin" "$ENV{VULKAN_SDK}/Bin")
elseif(SHADERC_COMBINED_LIBRARY)
    set(SHADERC_LIBRARY "${SHADERC_COMBINED_LIBRARY}")
endif()
if(NOT SHADERC_INCLUDE_DIR OR NOT SHADERC_LIBRARY)
    message(FATAL_ERROR "shaderc was not found. Set SHADERC_INCLUDE_DIR and SHADERC_LIBRARY.")
endif()
find_path(LEARN_VULKAN_INCLUDE_DIR NAMES vulkan/vk_platform.h
    HINTS "${SHADERC_INCLUDE_DIR}" "$ENV{VULKAN_SDK}/Include" "$ENV{VK_SDK_PATH}/Include")
if(LEARN_VULKAN_INCLUDE_DIR AND NOT TARGET Vulkan::Headers)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    set_target_properties(Vulkan::Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LEARN_VULKAN_INCLUDE_DIR}")
    get_filename_component(VULKAN_HEADERS_INSTALL_DIR "${LEARN_VULKAN_INCLUDE_DIR}" DIRECTORY)
elseif(NOT TARGET Vulkan::Headers)
    include(FetchContent)
    FetchContent_Declare(Vulkan-Headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers GIT_TAG v1.4.335)
    FetchContent_MakeAvailable(Vulkan-Headers)
endif()

set(RHI_HOST_SOURCES
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/render_pass_create_info.cpp"
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/pipeline_layout_create_info.cpp"
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/shader_module_create_info.cpp"
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/shader_program_create_info.cpp"
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/shader_reflect.cpp"
    "${PROJECT_SOURCE_DIR}/src/my_vulkan/pipeline/private/shader_program_reflection.cpp"
    "${PROJECT_SOURCE_DIR}/src/utility/shader_compiler.cpp")
if(NOT LEARN_VULKAN_BUILD_DEVICE)
    set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
    set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
    add_subdirectory(external/spirv_reflect)
endif()
add_library(rhi_host STATIC ${RHI_HOST_SOURCES})
target_include_directories(rhi_host PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/external/glm")
target_include_directories(rhi_host PRIVATE "${SHADERC_INCLUDE_DIR}" "${PROJECT_SOURCE_DIR}/external/spirv_reflect")
target_link_libraries(rhi_host PUBLIC Vulkan::Headers PRIVATE spirv-reflect-static "${SHADERC_LIBRARY}")
if(MSVC)
    target_compile_options(rhi_host PRIVATE /EHsc /utf-8)
endif()
enable_testing()
add_executable(rhi_host_tests src/create_info_finalize_host_tests.cpp src/render_pass_finalize_tests.cpp)
target_compile_definitions(rhi_host_tests PRIVATE RHI_HOST_TEST_MAIN)
target_link_libraries(rhi_host_tests PRIVATE rhi_host)
if(MSVC)
    target_compile_options(rhi_host_tests PRIVATE /EHsc /utf-8)
endif()
if(SHADERC_RUNTIME_LIBRARY)
    add_custom_command(TARGET rhi_host_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:rhi_host_tests>")
endif()
add_test(NAME rhi_host_tests COMMAND $<TARGET_FILE:rhi_host_tests>)
