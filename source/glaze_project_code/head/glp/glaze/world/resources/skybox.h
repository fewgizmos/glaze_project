#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"

#include "glp/glaze/rendering/vk/resources.h"

#include <vulkan/vulkan_core.h>

#include <glm/vec3.hpp>

#include <string>

namespace glaze::world::res
{

class cmp_cubemap : public scene::vol::volume_component
{
public:
    cmp_cubemap(const std::string& path, float intensity, float sun_intensity);
    ~cmp_cubemap() override;

    cmp_cubemap(const cmp_cubemap&) = delete;
    cmp_cubemap& operator=(const cmp_cubemap&) = delete;

    rendering::scene_render::gpu_model get_model() override;
    rendering::scene_render::gpu_environment get_environment() override;

    bool has_sun() const { return m_has_sun; }

    const scene::vol::component::light_create_info& get_sun() const { return m_sun; }
    glm::vec3 get_sun_rot() const { return m_sun_rot; }

private:
    rendering::image m_image;
    rendering::image m_irradiance;
    rendering::image m_prefiltered;

    float m_intensity = 1.0f;

    bool m_has_sun = false;

    scene::vol::component::light_create_info m_sun;
    glm::vec3 m_sun_rot = glm::vec3(0.0f);

    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;

    const rendering::scene_render::mesh* m_mesh = nullptr;
    const rendering::scene_render::material* m_material = nullptr;
};

class skybox : public scene::scene_entity
{
public:
    skybox(const std::string& path, float intensity = 1.0f, float sun_intensity = 1.0f);
};

}
