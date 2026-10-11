#pragma once

#include <glm/vec3.hpp>

#include <vector>

namespace glaze::scene::vol
{

class volume_component
{
public:
    volume_component(glm::vec3 pos = glm::vec3(0), glm::vec3 rot = glm::vec3(0)) : m_l_pos(pos), m_l_rot(rot) {};
    virtual ~volume_component() = default;

    glm::vec3 get_pos() { return m_l_pos; }
    glm::vec3 get_rot() { return m_l_rot; }

    void set_pos(glm::vec3 new_pos) { m_l_pos = new_pos; }
    void set_rot(glm::vec3 new_rot) { m_l_rot = new_rot; }

private:
    glm::vec3 m_l_pos;
    glm::vec3 m_l_rot;
};

struct scene_volume
{
    glm::vec3 w_pos = glm::vec3(0);
    glm::vec3 w_rot = glm::vec3(0);

    std::vector<volume_component*> components{};
};

}
