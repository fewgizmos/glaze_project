add_subdirectory(third_party)

file(GLOB_RECURSE GIZMOX_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_engine/impl/*.cpp
)

file(GLOB_RECURSE GIZMOX_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_engine/head/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_engine/head/*.hpp
)

add_executable(glaze_exec
    ${GIZMOX_SOURCES}
    ${GIZMOX_HEADERS}
)

target_include_directories(glaze_exec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_engine/head
)

target_link_libraries(glaze_exec PRIVATE
    SDL3::SDL3-static
    Vulkan::Vulkan
    glm::glm
    fastgltf::fastgltf
    imgui
    shaderc
    stb
    GPUOpen::VulkanMemoryAllocator
    vkbootstrap
)
