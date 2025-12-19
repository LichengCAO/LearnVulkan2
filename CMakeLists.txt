cmake_minimum_required(VERSION 3.10)
set(CMAKE_MOUDULE_PATH ${CMAKE_MOUDULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

project(learn_vulkan)

#find_package(Vulkan REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

##set output directory for realse and debug for visual studio
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} )
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY} )
endforeach()

# Source - https://stackoverflow.com/q
# Posted by Jepessen
# Retrieved 2025-12-19, License - CC BY-SA 4.0
# set up visual studio
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /Zc:preprocessor")
endif()

#show folder in IDE
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#add_subdirectory(src)
configure_file (
    "${PROJECT_SOURCE_DIR}/src/config.h.in"
    "${PROJECT_SOURCE_DIR}/src/config.h"
)

#ref: https://blog.csdn.net/vv1025/article/details/131117925
macro(source_group_by_dir source_files)
    if(MSVC)
        set(sgbd_cur_dir ${CMAKE_CURRENT_SOURCE_DIR})
        foreach(sgbd_file ${${source_files}})
            string(REGEX REPLACE ${sgbd_cur_dir}/\(.*\) \\1 sgbd_fpath ${sgbd_file})
            string(REGEX REPLACE "\(.*\)/.*" \\1 sgbd_group_name ${sgbd_fpath})
            string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
            string(REPLACE "/" "\\" sgbd_group_name ${sgbd_group_name})
            if(sgbd_nogroup)
                set(sgbd_group_name "\\")
            endif(sgbd_nogroup)
            source_group(${sgbd_group_name} FILES ${sgbd_file})
        endforeach(sgbd_file)
    endif(MSVC)
endmacro(source_group_by_dir)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h" "src/*.hpp")

set(ALL_FILES ${HEADERS} ${SOURCES})

source_group_by_dir(ALL_FILES)

add_executable(${CMAKE_PROJECT_NAME} ${ALL_FILES})

# Set the startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${CMAKE_PROJECT_NAME})

add_subdirectory(external)
add_subdirectory(res)
set_target_properties(ExternalLibs PROPERTIES FOLDER "ThirdParty")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "./src/")
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC ExternalLibs)
macro(include_directories_by_dir source_files)
    if(MSVC)
        set(sgbd_cur_dir ${CMAKE_CURRENT_SOURCE_DIR})
        foreach(sgbd_file ${${source_files}})
            string(REGEX REPLACE ${sgbd_cur_dir}/\(.*\) \\1 sgbd_fpath ${sgbd_file})
            string(REGEX REPLACE "\(.*\)/.*" \\1 sgbd_group_name ${sgbd_fpath})
            string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
            string(REPLACE "/" "\\" sgbd_group_name ${sgbd_group_name})
            if(sgbd_nogroup)
                set(sgbd_group_name "\\")
            endif(sgbd_nogroup)
            target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${sgbd_group_name})
        endforeach(sgbd_file)
    endif(MSVC)
endmacro(include_directories_by_dir)
include_directories_by_dir(ALL_FILES)

