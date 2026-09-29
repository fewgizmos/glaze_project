#pragma once

#include <deque>
#include <functional>
#include <utility>

namespace glaze::rendering::queue_deletor
{

inline std::deque<std::function<void()>> m_deletors;

inline void push(std::function<void()>&& function)
{
    m_deletors.push_back(std::move(function));
}

inline void clean_all()
{
    for (auto it = m_deletors.rbegin(); it != m_deletors.rend(); it++) {
        (*it)();
    }

    m_deletors.clear();
}

}
