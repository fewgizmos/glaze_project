#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering
{

enum class shader_stage
{
    vertex,
    fragment,
    compute
};

struct graphics_pipeline_create_info
{
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;

    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face    = VK_FRONT_FACE_CLOCKWISE;

    std::vector<VkDescriptorSetLayout> set_layouts;

    uint32_t push_constant_size = 0;
    VkShaderStageFlags push_constant_stages = 0;

    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
};

struct compute_pipeline_create_info
{
    std::vector<VkDescriptorSetLayout> set_layouts;

    uint32_t push_constant_size = 0;
};

struct pipeline
{
    VkPipeline handle = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

}

namespace glaze::rendering::vk::shaders
{

std::vector<uint32_t> compile_glsl(const std::string& source, shader_stage stage, const std::string& name);

VkShaderModule create_module(const std::vector<uint32_t>& spirv);
void destroy_module(VkShaderModule module);

VkShaderModule load_module(const std::string& file_name);

pipeline create_graphics_pipeline(
    const std::string& vert_file,
    const std::string& frag_file,
    const graphics_pipeline_create_info& create_info
);

pipeline create_compute_pipeline(const std::string& comp_file, const compute_pipeline_create_info& create_info);

void destroy_pipeline(const pipeline& target);

}
