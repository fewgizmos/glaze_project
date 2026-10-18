#pragma once

#include "glp/glaze/scene/objects/static_object.h"
#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace glaze::world::res
{

struct textured_plane_create_info
{
    glm::vec3 position = glm::vec3(0.0f);
    float rotation     = 0.0f;

    float size_x = 1.0f;
    float size_z = 1.0f;

    std::string pack_path;

    glm::vec2 tile_size = glm::vec2(0.0f);

    bool flip_normal_y = false;

    float transmission_factor = 1.0f;

    float parallax_strength = 0.5f;
};

class cmp_textured_plane : public scene::vol::volume_component
{
public:
    cmp_textured_plane(const textured_plane_create_info& create_info);
    ~cmp_textured_plane() override;

    void get_model(rendering::scene_render::gpu_model& out) override;

private:
    const rendering::scene_render::mesh* m_mesh = nullptr;
    const rendering::scene_render::material* m_material = nullptr;

    std::vector<const rendering::scene_render::texture*> m_textures;
};

class textured_plane : public scene::obj::static_object
{
public:
    textured_plane(const textured_plane_create_info& create_info);
};

}
