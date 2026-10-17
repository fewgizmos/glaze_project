#pragma once

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_environment.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"

#include "glp/glaze/utilities/state_machine.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <memory>
#include <vector>

namespace glaze::scene::vol
{

enum class component_flags : unsigned int
{
    modeled = 1U << 0,
    camera  = 1U << 1,
    light   = 1U << 2,
    post        = 1U << 3,
    environment = 1U << 4,
    atmosphere  = 1U << 5,
};

class volume_component
{
public:
    volume_component(glm::vec3 pos = glm::vec3(0), glm::vec3 rot = glm::vec3(0), glm::vec3 scale = glm::vec3(1))
        : m_l_pos(pos), m_l_rot(rot), m_l_scale(scale) {};
    virtual ~volume_component() = default;

    virtual void update() {}

    virtual rendering::scene_render::gpu_model get_model() { return {}; }
    virtual rendering::scene_render::gpu_camera get_camera() { return {}; }
    virtual rendering::scene_render::gpu_light get_light() { return {}; }
    virtual rendering::scene_render::gpu_post get_post() { return {}; }
    virtual rendering::scene_render::gpu_environment get_environment() { return {}; }
    virtual rendering::scene_render::gpu_atmosphere get_atmosphere() { return {}; }

    bool has_flag(component_flags flag) const { return m_flags.is_state_set(flag); }

    glm::vec3 get_pos() const   { return m_l_pos; }
    glm::vec3 get_rot() const   { return m_l_rot; }
    glm::vec3 get_scale() const { return m_l_scale; }

    void set_pos(glm::vec3 new_pos)     { m_l_pos = new_pos; }
    void set_rot(glm::vec3 new_rot)     { m_l_rot = new_rot; }
    void set_scale(glm::vec3 new_scale) { m_l_scale = new_scale; }

protected:
    void set_flag(component_flags flag) { m_flags.set_state(flag); }

private:
    glm::vec3 m_l_pos;
    glm::vec3 m_l_rot;
    glm::vec3 m_l_scale;

    utilities::sm::state<component_flags> m_flags{};
};

struct scene_volume
{
    glm::vec3 w_pos   = glm::vec3(0);
    glm::vec3 w_rot   = glm::vec3(0);
    glm::vec3 w_scale = glm::vec3(1);

    std::vector<std::unique_ptr<volume_component>> components{};
};

inline glm::mat4 compose_transform(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale)
{
    glm::mat4 result = glm::translate(glm::mat4(1.0f), pos);

    result = glm::rotate(result, glm::radians(rot.y), glm::vec3(0.0f, 1.0f, 0.0f));
    result = glm::rotate(result, glm::radians(rot.x), glm::vec3(1.0f, 0.0f, 0.0f));
    result = glm::rotate(result, glm::radians(rot.z), glm::vec3(0.0f, 0.0f, 1.0f));

    return glm::scale(result, scale);
}

}
