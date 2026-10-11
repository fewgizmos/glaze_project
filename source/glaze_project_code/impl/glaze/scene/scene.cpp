#include "glp/glaze/scene/scene.h"

#include <algorithm>

namespace glaze::scene::it
{

namespace
{

std::vector<std::unique_ptr<scene_entity>> m_entities;

}

scene_entity* push_builded_entity(std::unique_ptr<scene_entity>&& ent)
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
}

void erase_all_entities()
{
    m_entities.clear();
}

const std::vector<std::unique_ptr<scene_entity>>& get_all_entities()
{
    return m_entities;
}

size_t get_entities_count() { return m_entities.size(); }

}
