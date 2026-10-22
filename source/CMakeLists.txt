add_subdirectory(third_party)

file(GLOB_RECURSE GIZMOX_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_project_code/impl/*.cpp
)

file(GLOB_RECURSE GIZMOX_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_project_code/head/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_project_code/head/*.hpp
)

add_executable(glaze_executable
    ${GIZMOX_SOURCES}
    ${GIZMOX_HEADERS}
)

target_include_directories(glaze_executable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glaze_project_code/head
)

target_link_libraries(glaze_executable PRIVATE
    SDL3::SDL3-static
    Vulkan::Vulkan
    glm::glm
    fastgltf::fastgltf
    imgui
    shaderc
    stb
    GPUOpen::VulkanMemoryAllocator
    vkbootstrap
    ${CMAKE_DL_LIBS}
)

if(WIN32)
    target_link_libraries(glaze_executable PRIVATE pdh psapi)
endif()
