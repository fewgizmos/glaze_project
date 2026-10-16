#include "glp/glaze/scene/scene.h"

#include <algorithm>

namespace glaze::scene::it
{

namespace
{

std::vector<std::unique_ptr<scene_entity>> m_entities;

std::vector<std::unique_ptr<scene_entity>> m_pending_create;
std::vector<scene_entity*> m_pending_erase;

scene_entity* m_player_entity = nullptr;

void check_player_entity_alive()
{
    if (m_player_entity == nullptr) {
        return;
    }

    const auto is_player = [](const std::unique_ptr<scene_entity>& ent_ptr) { return ent_ptr.get() == m_player_entity; };

    const bool alive = std::find_if(m_entities.begin(), m_entities.end(), is_player) != m_entities.end()
                    || std::find_if(m_pending_create.begin(), m_pending_create.end(), is_player) != m_pending_create.end();

    if (!alive) {
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

namespace util
{

scene_entity* push_builded_entity(std::unique_ptr<scene_entity>&& ent)
{
    scene_entity* raw = ent.get();

    m_pending_create.push_back(std::move(ent));

    return raw;
}

bool is_entity_pending_erase(const scene_entity* ent)
{
    return std::find(m_pending_erase.begin(), m_pending_erase.end(), ent) != m_pending_erase.end();
}

}

void erase_entity(scene_entity* ent)
{
    auto pending = std::find_if(m_pending_create.begin(), m_pending_create.end(), [ent](const std::unique_ptr<scene_entity>& ent_ptr) { return ent_ptr.get() == ent; });

    if (pending != m_pending_create.end()) {
        m_pending_create.erase(pending);

        check_player_entity_alive();

        return;
    }

    const bool exists = std::find_if(m_entities.begin(), m_entities.end(), [ent](const std::unique_ptr<scene_entity>& ent_ptr) { return ent_ptr.get() == ent; }) != m_entities.end();

    if (exists && !util::is_entity_pending_erase(ent)) {
        m_pending_erase.push_back(ent);
    }
}

void erase_all_entities()
{
    m_pending_create.clear();

    for (const std::unique_ptr<scene_entity>& ent : m_entities) {
        if (!util::is_entity_pending_erase(ent.get())) {
            m_pending_erase.push_back(ent.get());
        }
    }

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
        if (util::is_entity_pending_erase(ent.get())) {
            continue;
        }

        ent->update();

        if (util::is_entity_pending_erase(ent.get())) {
            continue;
        }

        update_volume(ent->scene_volume());
        update_volume(ent->scene_gizmo_volume());
    }
}

void apply_pending_ops()
{
    std::erase_if(m_entities, [](const std::unique_ptr<scene_entity>& ent_ptr) { return util::is_entity_pending_erase(ent_ptr.get()); });

    m_pending_erase.clear();

    for (std::unique_ptr<scene_entity>& ent : m_pending_create) {
        m_entities.push_back(std::move(ent));
    }

    m_pending_create.clear();

    check_player_entity_alive();
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
