#include "glp/glaze/io/input.h"

#include "glp/glaze/platform/app.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace glaze::io::input
{

namespace
{

utilities::sm::state<keyboard_input_codes> m_previous_keys;
utilities::sm::state<keyboard_input_codes> m_current_keys;

mouse m_mouse;

bool map_key(SDL_Scancode scancode, keyboard_input_codes& code)
{
    switch (scancode) {
        case SDL_SCANCODE_F1: code = keyboard_input_codes::f1; return true;
        case SDL_SCANCODE_F2: code = keyboard_input_codes::f2; return true;
        case SDL_SCANCODE_F3: code = keyboard_input_codes::f3; return true;
        case SDL_SCANCODE_F4: code = keyboard_input_codes::f4; return true;
        case SDL_SCANCODE_F6: code = keyboard_input_codes::f6; return true;
        case SDL_SCANCODE_F8: code = keyboard_input_codes::f8; return true;

        case SDL_SCANCODE_GRAVE:  code = keyboard_input_codes::grave; return true;
        case SDL_SCANCODE_ESCAPE: code = keyboard_input_codes::esc;   return true;

        case SDL_SCANCODE_P: code = keyboard_input_codes::p; return true;
        case SDL_SCANCODE_Q: code = keyboard_input_codes::q; return true;

        case SDL_SCANCODE_W: code = keyboard_input_codes::w; return true;
        case SDL_SCANCODE_A: code = keyboard_input_codes::a; return true;
        case SDL_SCANCODE_S: code = keyboard_input_codes::s; return true;
        case SDL_SCANCODE_D: code = keyboard_input_codes::d; return true;

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: code = keyboard_input_codes::shift; return true;

        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: code = keyboard_input_codes::control; return true;

        case SDL_SCANCODE_SPACE:     code = keyboard_input_codes::space;     return true;
        case SDL_SCANCODE_RETURN:    code = keyboard_input_codes::enter;     return true;
        case SDL_SCANCODE_BACKSPACE: code = keyboard_input_codes::backspace; return true;
        case SDL_SCANCODE_TAB:       code = keyboard_input_codes::tab;       return true;

        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT: code = keyboard_input_codes::alt; return true;

        case SDL_SCANCODE_UP:    code = keyboard_input_codes::up;    return true;
        case SDL_SCANCODE_DOWN:  code = keyboard_input_codes::down;  return true;
        case SDL_SCANCODE_LEFT:  code = keyboard_input_codes::left;  return true;
        case SDL_SCANCODE_RIGHT: code = keyboard_input_codes::right; return true;

        case SDL_SCANCODE_E: code = keyboard_input_codes::e; return true;
        case SDL_SCANCODE_R: code = keyboard_input_codes::r; return true;
        case SDL_SCANCODE_T: code = keyboard_input_codes::t; return true;
        case SDL_SCANCODE_F: code = keyboard_input_codes::f; return true;
        case SDL_SCANCODE_G: code = keyboard_input_codes::g; return true;
        case SDL_SCANCODE_V: code = keyboard_input_codes::v; return true;
        case SDL_SCANCODE_M: code = keyboard_input_codes::m; return true;
        case SDL_SCANCODE_H: code = keyboard_input_codes::h; return true;
        case SDL_SCANCODE_Z: code = keyboard_input_codes::z; return true;
        case SDL_SCANCODE_X: code = keyboard_input_codes::x; return true;
        case SDL_SCANCODE_I: code = keyboard_input_codes::i; return true;
        case SDL_SCANCODE_Y: code = keyboard_input_codes::y; return true;
        case SDL_SCANCODE_N: code = keyboard_input_codes::n; return true;

        default: return false;
    }
}

bool map_button(Uint8 button, mouse_input_codes& code)
{
    switch (button) {
        case SDL_BUTTON_LEFT:   code = mouse_input_codes::left;   return true;
        case SDL_BUTTON_MIDDLE: code = mouse_input_codes::middle; return true;
        case SDL_BUTTON_RIGHT:  code = mouse_input_codes::right;  return true;

        case SDL_BUTTON_X1: code = mouse_input_codes::side_1; return true;
        case SDL_BUTTON_X2: code = mouse_input_codes::side_2; return true;

        default: return false;
    }
}

void parse_key(const SDL_Event& event)
{
    keyboard_input_codes code;

    if (!map_key(event.key.scancode, code)) {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN) {
        m_current_keys.set_state(code);
    } else {
        m_current_keys.clear_state(code);
    }
}

void parse_button(const SDL_Event& event)
{
    mouse_input_codes code;

    if (!map_button(event.button.button, code)) {
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        m_mouse.buttons_input_codes.set_state(code);
    } else {
        m_mouse.buttons_input_codes.clear_state(code);
    }
}

}

void parse_frame_inputs()
{
    m_previous_keys = m_current_keys;

    m_mouse.previous_buttons_input_codes = m_mouse.buttons_input_codes;

    m_mouse.delta_x = 0.0f;
    m_mouse.delta_y = 0.0f;

    m_mouse.scroll_x = 0.0f;
    m_mouse.scroll_y = 0.0f;

    for (const SDL_Event& event : platform::app::get_events()) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                parse_key(event);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                parse_button(event);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_mouse.pos_x = event.motion.x;
                m_mouse.pos_y = event.motion.y;

                m_mouse.delta_x += event.motion.xrel;
                m_mouse.delta_y += event.motion.yrel;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                m_mouse.scroll_x += event.wheel.x;
                m_mouse.scroll_y += event.wheel.y;
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_current_keys.clear_all_states();

                m_mouse.buttons_input_codes.clear_all_states();
                break;

            default:
                break;
        }
    }
}

bool key_is_pressed(keyboard_input_codes code)
{
    return !m_previous_keys.is_state_set(code) && m_current_keys.is_state_set(code);
}

bool key_is_held(keyboard_input_codes code)
{
    return m_previous_keys.is_state_set(code) && m_current_keys.is_state_set(code);
}

bool key_is_released(keyboard_input_codes code)
{
    return m_previous_keys.is_state_set(code) && !m_current_keys.is_state_set(code);
}

bool key_is_up(keyboard_input_codes code)
{
    return !m_previous_keys.is_state_set(code) && !m_current_keys.is_state_set(code);
}

const mouse& get_mouse() { return m_mouse; }

}
