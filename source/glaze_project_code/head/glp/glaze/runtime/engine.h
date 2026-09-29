#pragma once

namespace glaze::runtime::engine
{

inline bool wants_stop = false;

int run();

void startup();
void cleanup();

void tick_frame();

}
