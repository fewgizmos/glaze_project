#include "glp/trainer/scene/debug_camera.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_camera.h"

#include "glp/glaze/io/input.h"
#include "glp/glaze/io/viewport.h"

#include "glp/glaze/platform/window_manager.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace trainer::scene
{

namespace
{

constexpr float START_SPEED = 10.0f;
constexpr float MIN_SPEED   = 0.1f;
constexpr float MAX_SPEED   = 600.0f;

constexpr float SPEED_SCROLL_FACTOR = 1.2f;

constexpr float MOUSE_SENSITIVITY = 0.1f;

}

debug_camera_player_obj::debug_camera_player_obj() : m_speed(START_SPEED)
{
    m_scene_volume().components.push_back(std::make_unique<glaze::scene::vol::component::camera>());
}

void debug_camera_player_obj::update()
{
    if (glaze::scene::it::get_player_entity() != this) {
        return;
    }

    const glaze::io::input::mouse& mouse = glaze::io::input::get_mouse();

    glaze::scene::vol::scene_volume& volume = m_scene_volume();

    if (mouse.button_is_pressed(glaze::io::input::mouse_input_codes::right)) {
        glaze::platform::wm::capture_cursor(glaze::io::viewport::m_win);
    }

    if (mouse.button_is_released(glaze::io::input::mouse_input_codes::right)) {
        glaze::platform::wm::release_cursor(glaze::io::viewport::m_win);
    }

    if (mouse.button_is_held(glaze::io::input::mouse_input_codes::right)) {
        volume.w_rot.y -= mouse.delta_x * MOUSE_SENSITIVITY;
        volume.w_rot.x -= mouse.delta_y * MOUSE_SENSITIVITY;
    }

    if (mouse.scroll_y != 0.0f) {
        m_speed = std::clamp(m_speed * std::pow(SPEED_SCROLL_FACTOR, mouse.scroll_y), MIN_SPEED, MAX_SPEED);
    }

    const glm::mat4 rotation = glaze::scene::vol::compose_transform(glm::vec3(0.0f), volume.w_rot, glm::vec3(1.0f));

    const glm::vec3 forward = glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    const glm::vec3 right   = glm::vec3(rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 direction(0.0f);

    using glaze::io::input::keyboard_input_codes;
    using glaze::io::input::key_is_held;
    using glaze::io::input::key_is_pressed;

    if (key_is_pressed(keyboard_input_codes::w) || key_is_held(keyboard_input_codes::w)) { direction += forward; }
    if (key_is_pressed(keyboard_input_codes::s) || key_is_held(keyboard_input_codes::s)) { direction -= forward; }
    if (key_is_pressed(keyboard_input_codes::d) || key_is_held(keyboard_input_codes::d)) { direction += right;   }
    if (key_is_pressed(keyboard_input_codes::a) || key_is_held(keyboard_input_codes::a)) { direction -= right;   }
    if (key_is_pressed(keyboard_input_codes::e) || key_is_held(keyboard_input_codes::e)) { direction += up;      }
    if (key_is_pressed(keyboard_input_codes::q) || key_is_held(keyboard_input_codes::q)) { direction -= up;      }

    if (glm::length(direction) > 0.0f) {
        volume.w_pos += glm::normalize(direction) * m_speed * glaze::utilities::core::timer::delta;
    }
}

}

