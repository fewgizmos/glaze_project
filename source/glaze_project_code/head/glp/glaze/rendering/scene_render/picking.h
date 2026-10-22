#pragma once

#include <glm/vec2.hpp>

#include <cstdint>

namespace glaze::rendering::scene_render::picking
{

constexpr uint32_t ENTITY_ID_BITS = 22;
constexpr uint64_t ENTITY_ID_MASK = (uint64_t(1) << ENTITY_ID_BITS) - 1;

struct pick_result
{
    uint64_t request = 0;

    uint32_t entity_id = 0;

    float depth = 0.0f;

    glm::vec2 uv = glm::vec2(0.0f);
};

void init_all();
void clean_all();

uint64_t request(glm::vec2 uv);

void dispatch();

bool take_result(pick_result& out);

}
