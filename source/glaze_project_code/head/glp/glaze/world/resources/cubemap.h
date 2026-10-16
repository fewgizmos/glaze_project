#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"

#include "glp/glaze/rendering/vk/resources.h"

#include <vulkan/vulkan_core.h>

#include <string>

namespace glaze::world::res
{

class cmp_cubemap : public scene::vol::volume_component
{
public:
    cmp_cubemap(const std::string& dir, float intensity);
    ~cmp_cubemap() override;

    cmp_cubemap(const cmp_cubemap&) = delete;
    cmp_cubemap& operator=(const cmp_cubemap&) = delete;

    rendering::scene_render::gpu_model get_model() override;
    rendering::scene_render::gpu_environment get_environment() override;

private:
    rendering::image m_image;
    rendering::image m_irradiance;
    rendering::image m_prefiltered;

    float m_intensity = 1.0f;

    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;

    const rendering::scene_render::mesh* m_mesh = nullptr;
    const rendering::scene_render::material* m_material = nullptr;
};

class cubemap : public scene::scene_entity
{
public:
    cubemap(const std::string& dir, float intensity = 1.0f);
};

}
