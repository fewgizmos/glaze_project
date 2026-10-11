#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include <vector>
#include <memory>

namespace glaze::scene
{

class scene_entity
{
public:
    virtual ~scene_entity() = default;

    const vol::scene_volume& scene_volume() { return d_scene_volume; }
    const vol::scene_volume& scene_gizmo_volume() { return d_gizmo_scene_volume; }

protected:
    vol::scene_volume& m_scene_volume() { return d_scene_volume; }
    vol::scene_volume& m_scene_gizmo_volume() { return d_gizmo_scene_volume; }

private:
    vol::scene_volume d_scene_volume;
    vol::scene_volume d_gizmo_scene_volume;
};

namespace it
{

template<typename ent_type, typename... args_type>
ent_type* create_entity(args_type&&... args);

void erase_entity(scene_entity* ent);

void erase_all_entities();

template<typename ent_type>
void erase_all_entities_typed();

const std::vector<std::unique_ptr<scene_entity>>& get_all_entities();

template<typename ent_type>
std::vector<ent_type*> get_all_entities_typed();

size_t get_entities_count();

}

}
