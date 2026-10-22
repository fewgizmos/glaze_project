find_package(Vulkan REQUIRED GLOBAL)

# sdl3
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
add_subdirectory(sdl3 EXCLUDE_FROM_ALL)

# glm
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glm EXCLUDE_FROM_ALL)

# vma
set(VMA_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
add_subdirectory(vma EXCLUDE_FROM_ALL)

# fastgltf
set(FASTGLTF_ENABLE_TESTS     OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS      OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON  CACHE BOOL "" FORCE)
add_subdirectory(fastgltf EXCLUDE_FROM_ALL)

# stb
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# vkbootstrap
add_library(vkbootstrap STATIC
    vkbootstrap/src/VkBootstrap.cpp
)
target_include_directories(vkbootstrap
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap/include/vkb
)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan ${CMAKE_DL_LIBS})

# shaderc
find_package(glslang REQUIRED GLOBAL)
find_package(SPIRV-Tools REQUIRED GLOBAL)
find_package(SPIRV-Tools-opt REQUIRED GLOBAL)

if(NOT TARGET glslang)
    add_library(glslang ALIAS glslang::glslang)
endif()

if(NOT TARGET SPIRV)
    add_library(SPIRV ALIAS glslang::SPIRV)
endif()

if(NOT TARGET SPIRV-Tools)
    add_library(SPIRV-Tools ALIAS SPIRV-Tools-static)
endif()

find_path(GLSLANG_SOURCE_LIKE_DIR NAMES SPIRV/GlslangToSpv.h PATH_SUFFIXES glslang REQUIRED)
set(glslang_SOURCE_DIR ${GLSLANG_SOURCE_LIKE_DIR})

set(SHADERC_SKIP_TESTS           ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES        ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES     ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL         ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON  CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
add_subdirectory(shaderc EXCLUDE_FROM_ALL)

# imgui
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)

# imguizmo
add_library(imguizmo STATIC
    imguizmo/ImGuizmo.cpp
)
target_include_directories(imguizmo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo)
target_link_libraries(imguizmo PUBLIC imgui)

# imguifiledialog
find_package(Threads REQUIRED)

add_library(imguifiledialog STATIC
    imguifiledialog/ImGuiFileDialog.cpp
)
target_include_directories(imguifiledialog
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/imguifiledialog
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(imguifiledialog
    PUBLIC  USE_STD_FILESYSTEM USE_THUMBNAILS
    PRIVATE DONT_DEFINE_AGAIN__STB_IMAGE_IMPLEMENTATION
)
target_link_libraries(imguifiledialog PUBLIC imgui Threads::Threads)
