#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"
#include "glp/glaze/utilities/fs.h"

#include <shaderc/shaderc.hpp>

#include <filesystem>

namespace glaze::rendering::vk::shaders
{

namespace
{

shaderc_shader_kind get_shaderc_kind(shader_stage stage)
{
    switch (stage) {
        case shader_stage::vertex:   return shaderc_glsl_vertex_shader;
        case shader_stage::fragment: return shaderc_glsl_fragment_shader;
        case shader_stage::compute:  return shaderc_glsl_compute_shader;
    }

    return shaderc_glsl_infer_from_source;
}

shader_stage get_stage_from_file(const std::string& file_name)
{
    const std::string extension = std::filesystem::path(file_name).extension().string();

    if (extension == ".vert") { return shader_stage::vertex;   }
    if (extension == ".frag") { return shader_stage::fragment; }
    if (extension == ".comp") { return shader_stage::compute;  }

    GLAZE_ASSERT(true, "unknown shader extension: " + file_name);

    return shader_stage::vertex;
}

}

std::vector<uint32_t> compile_glsl(const std::string& source, shader_stage stage, const std::string& name)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);

#ifdef NDEBUG
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
#else
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
    options.SetGenerateDebugInfo();
#endif

    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source,
        get_shaderc_kind(stage),
        name.c_str(),
        options
    );

    GLAZE_ASSERT(
        result.GetCompilationStatus() != shaderc_compilation_status_success,
        "failed to compile shader " + name + "\n" + result.GetErrorMessage()
    );

    return { result.cbegin(), result.cend() };
}

VkShaderModule create_module(const std::vector<uint32_t>& spirv)
{
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = spirv.size() * sizeof(uint32_t);
    create_info.pCode = spirv.data();

    VkShaderModule shader_module = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().createShaderModule(&create_info, nullptr, &shader_module);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create shader module");

    return shader_module;
}

void destroy_module(VkShaderModule module)
{
    device::get_disp().destroyShaderModule(module, nullptr);
}

VkShaderModule load_module(const std::string& file_name)
{
    const std::string source = utilities::fs::read_file(std::string(SHADERS_DIR) + file_name);

    return create_module(compile_glsl(source, get_stage_from_file(file_name), file_name));
}

pipeline create_graphics_pipeline(const graphics_pipeline_info& info)
{
    const vkb::DispatchTable& disp = device::get_disp();

    VkShaderModule vert_module = load_module(info.vert_file);
    VkShaderModule frag_module = load_module(info.frag_file);

    VkPipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert_module;
    vert_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage_info.module = frag_module;
    frag_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 0;
    vertex_input_info.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = info.cull_mode;
    rasterizer.frontFace = info.front_face;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 0;

    pipeline result_pipeline;

    VkResult result = disp.createPipelineLayout(&pipeline_layout_info, nullptr, &result_pipeline.layout);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create pipeline layout");

    const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_info = {};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = 2;
    dynamic_info.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &info.color_format;
    rendering_info.depthAttachmentFormat = info.depth_format;
    rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.layout = result_pipeline.layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    result = disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &result_pipeline.handle);

    destroy_module(frag_module);
    destroy_module(vert_module);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create graphics pipeline");

    return result_pipeline;
}

void destroy_pipeline(const pipeline& target)
{
    device::get_disp().destroyPipeline(target.handle, nullptr);
    device::get_disp().destroyPipelineLayout(target.layout, nullptr);
}

}
