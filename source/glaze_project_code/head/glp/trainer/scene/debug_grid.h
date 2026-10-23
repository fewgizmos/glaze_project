#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"

namespace trainer::scene
{

class cmp_debug_grid : public glaze::scene::vol::volume_component
{
public:
    cmp_debug_grid();
    ~cmp_debug_grid() override;

    cmp_debug_grid(const cmp_debug_grid&) = delete;
    cmp_debug_grid& operator=(const cmp_debug_grid&) = delete;

    void get_model(glaze::rendering::scene_render::gpu_model& out) override;

private:
    const glaze::rendering::scene_render::mesh* m_mesh = nullptr;
    const glaze::rendering::scene_render::material* m_material = nullptr;
};

class debug_grid_object : public glaze::scene::scene_entity
{
public:
    debug_grid_object();
};

}

#endif
