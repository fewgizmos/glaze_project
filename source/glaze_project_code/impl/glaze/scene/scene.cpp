#include "glp/glaze/scene/scene.h"

#include <algorithm>

namespace glaze::scene::it
{

namespace
{

std::vector<std::unique_ptr<scene_entity>> m_entities;

scene_entity* m_player_entity = nullptr;

void check_player_entity_alive()
{
    if (m_player_entity == nullptr) {
        return;
    }

    const auto found = std::find_if(m_entities.begin(), m_entities.end(), [](const std::unique_ptr<scene_entity>& ent_ptr) { return ent_ptr.get() == m_player_entity; });

    if (found == m_entities.end()) {
        m_player_entity = nullptr;
    }
}

void update_volume(const vol::scene_volume& volume)
{
    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        comp->update();
    }
}

}

scene_entity* u_push_builded_entity(std::unique_ptr<scene_entity>&& ent)
{
    scene_entity* raw = ent.get();

    m_entities.push_back(std::move(ent));

    return raw;
}

void erase_entity(scene_entity* ent)
{
    auto it = std::find_if(m_entities.begin(), m_entities.end(), [ent](const std::unique_ptr<scene_entity>& ent_ptr) { return ent_ptr.get() == ent; });

    if (it != m_entities.end()) {
        std::swap(*it, m_entities.back());
        m_entities.pop_back();
    }

    check_player_entity_alive();
}

void erase_all_entities()
{
    m_entities.clear();

    check_player_entity_alive();
}

const std::vector<std::unique_ptr<scene_entity>>& get_all_entities()
{
    return m_entities;
}

size_t get_entities_count() { return m_entities.size(); }

void update_entities()
{
    for (const std::unique_ptr<scene_entity>& ent : m_entities) {
        ent->update();

        update_volume(ent->scene_volume());
        update_volume(ent->scene_gizmo_volume());
    }
}

void set_player_entity(scene_entity* ent)
{
    m_player_entity = ent;
}

scene_entity* get_player_entity()
{
    return m_player_entity;
}

}
