#pragma once

#include <type_traits>

namespace glaze::utilities::sm
{

template <typename enum_type>
class state
{
    public:
        using storage = typename std::underlying_type<enum_type>::type;

        state() = default;
        explicit state(enum_type initial_state)
            : m_state(static_cast<storage>(initial_state)) {}

        void set_state(enum_type state_flag)    { m_state |= static_cast<storage>(state_flag); }
        void toggle_state(enum_type state_flag) { m_state ^= static_cast<storage>(state_flag); }

        void clear_state(enum_type state_flag)
        {
            m_state &= static_cast<storage>(~static_cast<storage>(state_flag));
        }

        bool is_state_set(enum_type state_flag) const
        {
            return (m_state & static_cast<storage>(state_flag)) != 0;
        }

        void clear_all_states() { m_state = 0; }

        storage get_raw() const { return m_state; }

    private:
        storage m_state = 0;
};

}

