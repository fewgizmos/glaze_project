#include "glp/glaze/scene/scene.h"

#include <type_traits>
#include <algorithm>

namespace glaze::scene::it
{

namespace
{

std::vector<std::unique_ptr<scene_entity>> m_entities;

}

template<typename ent_type, typename... args_type>
ent_type* create_entity(args_type&&... args)
{
    static_assert(std::is_base_of_v<scene_entity, ent_type>, "entity object must derive from scene_entity");

    auto obj = std::make_unique<ent_type>(std::forward<args_type>(args)...);
    ent_type* raw = obj.get();

    m_entities.push_back(std::move(obj));

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

template<typename ent_type>
void erase_all_entities_typed()
{
    static_assert(std::is_base_of_v<scene_entity, ent_type>, "entity object must derive from scene_entity");

    std::erase_if(m_entities, [](const std::unique_ptr<scene_entity>& ent_ptr) {
        return dynamic_cast<ent_type*>(ent_ptr.get()) != nullptr;
    });
}

const std::vector<std::unique_ptr<scene_entity>>& get_all_entities()
{
    return m_entities;
}

template<typename ent_type>
std::vector<ent_type*> get_all_entities_typed()
{
    static_assert(std::is_base_of_v<scene_entity, ent_type>, "entity object must derive from scene_entity");

    std::vector<ent_type*> out;
    for (const auto& ent_ptr : m_entities)
        if (auto* el = dynamic_cast<ent_type*>(ent_ptr.get()))
            out.push_back(el);
    return out;
}

size_t get_entities_count() { return m_entities.size(); }

}
