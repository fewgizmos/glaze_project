#ifndef NDEBUG

#include "glp/trainer/core/trainer.h"

#include "glp/trainer/scene/debug_grid.h"
#include "glp/trainer/ui/ui.h"

#include "glp/glaze/scene/scene.h"

namespace trainer::core::it
{

namespace
{

scene::debug_grid_object* m_debug_grid = nullptr;

}

void startup()
{
    m_debug_grid = glaze::scene::it::create_entity<scene::debug_grid_object>();
}

void cleanup()
{
    ui::it::cleanup();

    glaze::scene::it::erase_entity(m_debug_grid);

    m_debug_grid = nullptr;
}

void frame_update()
{
    ui::it::draw();
}

}

#endif
