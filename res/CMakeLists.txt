file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    "shaders/*.vert"
    "shaders/*.frag"
    "shaders/*.geom"
    "shaders/*.comp"
    "shaders/*.tesc"
    "shaders/*.tese"
    "shaders/*.mesh"
    "shaders/*.task"
    "shaders/*.rgen"
    "shaders/*.rmiss"
    "shaders/*.rchit"
    "shaders/*.rahit"
    "shaders/*.rcall"
    "shaders/*.rint"
)

file(GLOB IMAGES CONFIGURE_DEPENDS
    "images/*.png"
    "images/*.jpg"
    "images/*.hdr"
    "images/*.bmp"
)

foreach(IMAGE ${IMAGES})
    get_filename_component(fname "${IMAGE}" NAME)
    configure_file("${IMAGE}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/images/${fname}" COPYONLY)
endforeach()

if(SHADER_SOURCES)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED
        HINTS ENV VK_SDK_PATH ENV VULKAN_SDK PATH_SUFFIXES bin)
endif()
set(SHADER_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(fname "${SHADER}" NAME)
    set(output "${SHADER_DIR}/${fname}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}" --target-env vulkan1.2 -V "${SHADER}"
            -o "${output}" -g --depfile "${output}.d"
        DEPENDS "${SHADER}" "${GLSLANG_VALIDATOR}"
        DEPFILE "${output}.d" VERBATIM)
    add_custom_target(${fname}.spv DEPENDS "${output}" SOURCES "${SHADER}")
    set_property(TARGET ${fname}.spv PROPERTY FOLDER "CompiledSPV")
    add_dependencies(learn_vulkan ${fname}.spv)
endforeach()
