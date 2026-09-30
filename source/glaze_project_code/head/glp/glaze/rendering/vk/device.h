#pragma once

#include "glp/glaze/rendering/types.h"

namespace glaze::rendering::vk::device
{

void create_surface(const vkb::Instance& instance);
void create_device(const vkb::Instance& instance);

void make_queues();

const vkb::PhysicalDevice& get_physical();
const vkb::Device& get_device();
const vkb::DispatchTable& get_disp();

VkSurfaceKHR get_surface();

const queue_set& get_queues();

}
