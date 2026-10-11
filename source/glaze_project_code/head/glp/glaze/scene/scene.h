#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include <vector>
#include <memory>
#include <type_traits>
#include <utility>

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

scene_entity* push_builded_entity(std::unique_ptr<scene_entity>&& ent);

template<typename ent_type, typename... args_type>
ent_type* create_entity(args_type&&... args)
{
    static_assert(std::is_base_of_v<scene_entity, ent_type>, "entity object must derive from scene_entity");

    return static_cast<ent_type*>(push_builded_entity(std::make_unique<ent_type>(std::forward<args_type>(args)...)));
}

void erase_entity(scene_entity* ent);

void erase_all_entities();

const std::vector<std::unique_ptr<scene_entity>>& get_all_entities();

template<typename ent_type>
std::vector<ent_type*> get_all_entities_typed()
{
    static_assert(std::is_base_of_v<scene_entity, ent_type>, "entity object must derive from scene_entity");

    std::vector<ent_type*> out;
    for (const auto& ent_ptr : get_all_entities())
        if (auto* el = dynamic_cast<ent_type*>(ent_ptr.get()))
            out.push_back(el);
    return out;
}

template<typename ent_type>
void erase_all_entities_typed()
{
    for (ent_type* ent : get_all_entities_typed<ent_type>())
        erase_entity(ent);
}

size_t get_entities_count();

}

}
