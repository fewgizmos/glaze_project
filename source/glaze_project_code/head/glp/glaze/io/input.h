#pragma once

#include "glp/glaze/utilities/state_machine.h"

namespace glaze::io::input
{

enum class keyboard_input_codes : unsigned long long
{
    f1 = 1ULL << 0,
    f2 = 1ULL << 1,
    f3 = 1ULL << 2,
    f4 = 1ULL << 3,
    f6 = 1ULL << 4,
    f8 = 1ULL << 5,

    grave = 1ULL << 6,
    esc   = 1ULL << 7,

    p = 1ULL << 8,
    q = 1ULL << 9,

    w = 1ULL << 10,
    a = 1ULL << 11,
    s = 1ULL << 12,
    d = 1ULL << 13,

    shift     = 1ULL << 14,
    control   = 1ULL << 15,
    space     = 1ULL << 16,
    enter     = 1ULL << 17,
    backspace = 1ULL << 18,
    tab       = 1ULL << 19,
    alt       = 1ULL << 20,

    up    = 1ULL << 21,
    down  = 1ULL << 22,
    left  = 1ULL << 23,
    right = 1ULL << 24,

    e = 1ULL << 25,
    r = 1ULL << 26,
    t = 1ULL << 27,
    f = 1ULL << 28,
    g = 1ULL << 29,
    v = 1ULL << 30,
    m = 1ULL << 31,
    h = 1ULL << 32,
    z = 1ULL << 33,
    x = 1ULL << 34,
    i = 1ULL << 35,
    y = 1ULL << 36,
    n = 1ULL << 37,
};

enum class mouse_input_codes : unsigned int
{
    left   = 1U << 0,
    middle = 1U << 1,
    right  = 1U << 2,

    side_1 = 1U << 3,
    side_2 = 1U << 4,
};

struct mouse
{
    float pos_x = 0.0f;
    float pos_y = 0.0f;

    float delta_x = 0.0f;
    float delta_y = 0.0f;

    float scroll_x = 0.0f;
    float scroll_y = 0.0f;

    utilities::sm::state<mouse_input_codes> buttons_input_codes{};
    utilities::sm::state<mouse_input_codes> previous_buttons_input_codes{};

    bool button_is_pressed(mouse_input_codes code) const
    {
        return !previous_buttons_input_codes.is_state_set(code) && buttons_input_codes.is_state_set(code);
    }

    bool button_is_held(mouse_input_codes code) const
    {
        return previous_buttons_input_codes.is_state_set(code) && buttons_input_codes.is_state_set(code);
    }

    bool button_is_released(mouse_input_codes code) const
    {
        return previous_buttons_input_codes.is_state_set(code) && !buttons_input_codes.is_state_set(code);
    }

    bool button_is_up(mouse_input_codes code) const
    {
        return !previous_buttons_input_codes.is_state_set(code) && !buttons_input_codes.is_state_set(code);
    }
};

void parse_frame_inputs();

void set_capture(bool mouse, bool keyboard);

bool key_is_pressed(keyboard_input_codes code);
bool key_is_held(keyboard_input_codes code);
bool key_is_released(keyboard_input_codes code);
bool key_is_up(keyboard_input_codes code);

const mouse& get_mouse();

}
