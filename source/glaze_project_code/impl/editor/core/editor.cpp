#ifndef NDEBUG

#include "glp/editor/core/editor.h"

#include "glp/editor/scene/debug_grid.h"
#include "glp/editor/ui/ui.h"

#include "glp/glaze/scene/scene.h"

namespace editor::core::it
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
    glaze::scene::it::erase_entity(m_debug_grid);

    m_debug_grid = nullptr;
}

void frame_update()
{
    ui::it::draw();
}

}

#endif
