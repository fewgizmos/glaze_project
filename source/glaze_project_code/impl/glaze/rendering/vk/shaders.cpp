#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"
#include "glp/glaze/utilities/file.h"

#include <shaderc/shaderc.hpp>

#include <exception>
#include <filesystem>
#include <memory>

namespace glaze::rendering::vk::shaders
{

namespace
{

constexpr const char* FRAGMENT_STAGE_DEFINE = "GLAZE_FRAGMENT_STAGE";

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

std::string resolve_include(const std::string& requested_source, const std::string& requesting_source)
{
    std::filesystem::path directory = std::filesystem::path(requesting_source).parent_path();

    while (true) {
        const std::filesystem::path candidate = (directory / requested_source).lexically_normal();

        if (std::filesystem::exists(std::filesystem::path(SHADERS_DIR) / candidate)) {
            return candidate.generic_string();
        }

        if (directory.empty()) {
            return requested_source;
        }

        directory = directory.parent_path();
    }
}

struct include_data
{
    shaderc_include_result result = {};

    std::string name;
    std::string content;
};

class shader_includer : public shaderc::CompileOptions::IncluderInterface
{
    public:
        shaderc_include_result* GetInclude(
            const char* requested_source,
            shaderc_include_type type,
            const char* requesting_source,
            size_t include_depth
        ) override
        {
            std::unique_ptr<include_data> data = std::make_unique<include_data>();

            try {
                data->name    = resolve_include(requested_source, requesting_source);
                data->content = utilities::file::read_raw(std::string(SHADERS_DIR) + data->name);
            }

            catch (const std::exception& error) {
                data->content = error.what();
            }

            data->result.source_name        = data->name.c_str();
            data->result.source_name_length = data->name.size();
            data->result.content            = data->content.c_str();
            data->result.content_length     = data->content.size();
            data->result.user_data          = data.get();

            shaderc_include_result* result = &data->result;

            m_includes.push_back(std::move(data));

            return result;
        }

        void ReleaseInclude(shaderc_include_result* result) override
        {
            std::erase_if(m_includes, [result](const std::unique_ptr<include_data>& stored) {
                return stored.get() == result->user_data;
            });
        }

    private:
        std::vector<std::unique_ptr<include_data>> m_includes;
};

}

std::vector<uint32_t> compile_glsl(const std::string& source, shader_stage stage, const std::string& name, const std::vector<std::string>& defines)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    for (const std::string& define : defines) {
        const size_t separator = define.find('=');

        if (separator == std::string::npos) {
            options.AddMacroDefinition(define);
        } else {
            options.AddMacroDefinition(define.substr(0, separator), define.substr(separator + 1));
        }
    }

    if (stage == shader_stage::fragment) {
        options.AddMacroDefinition(FRAGMENT_STAGE_DEFINE);
    }

    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetIncluder(std::make_unique<shader_includer>());

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
    const std::string source = utilities::file::read_raw(std::string(SHADERS_DIR) + file_name);

    return create_module(compile_glsl(source, get_stage_from_file(file_name), file_name));
}

pipeline create_graphics_pipeline(
    const std::string& vert_file,
    const std::string& frag_file,
    const graphics_pipeline_create_info& create_info
)
{
    const vkb::DispatchTable& disp = device::get_disp();

    const uint32_t color_count = static_cast<uint32_t>(create_info.color_formats.size());

    const std::vector<uint32_t> vert_spirv = compile_glsl(utilities::file::read_raw(std::string(SHADERS_DIR) + vert_file), shader_stage::vertex, vert_file, create_info.defines);
    const std::vector<uint32_t> frag_spirv = frag_file.empty() ? std::vector<uint32_t>() : compile_glsl(utilities::file::read_raw(std::string(SHADERS_DIR) + frag_file), shader_stage::fragment, frag_file, create_info.defines);

    VkShaderModule vert_module = create_module(vert_spirv);
    VkShaderModule frag_module = frag_file.empty() ? VK_NULL_HANDLE : create_module(frag_spirv);

    VkPipelineShaderStageCreateInfo vert_stage_create_info = {};
    vert_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_create_info.module = vert_module;
    vert_stage_create_info.pName = "main";

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages = { vert_stage_create_info };

    if (frag_module != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo frag_stage_create_info = {};
        frag_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage_create_info.module = frag_module;
        frag_stage_create_info.pName = "main";

        shader_stages.push_back(frag_stage_create_info);
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_create_info = {};
    vertex_input_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_create_info.vertexBindingDescriptionCount = static_cast<uint32_t>(create_info.vertex_bindings.size());
    vertex_input_create_info.pVertexBindingDescriptions = create_info.vertex_bindings.data();
    vertex_input_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(create_info.vertex_attributes.size());
    vertex_input_create_info.pVertexAttributeDescriptions = create_info.vertex_attributes.data();

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
    rasterizer.depthClampEnable = create_info.depth_clamp ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = create_info.cull_mode;
    rasterizer.frontFace = create_info.front_face;
    rasterizer.depthBiasEnable = create_info.depth_bias_constant != 0.0f || create_info.depth_bias_slope != 0.0f ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = create_info.depth_bias_constant;
    rasterizer.depthBiasSlopeFactor = create_info.depth_bias_slope;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = create_info.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = create_info.depth_compare;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = create_info.alpha_blend ? VK_TRUE : VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    const std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(color_count, color_blend_attachment);

    color_blending.attachmentCount = color_count;
    color_blending.pAttachments = color_count > 0 ? color_blend_attachments.data() : nullptr;

    VkPushConstantRange push_range = {};
    push_range.stageFlags = create_info.push_constant_stages;
    push_range.offset = 0;
    push_range.size = create_info.push_constant_size;

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(create_info.set_layouts.size());
    pipeline_layout_create_info.pSetLayouts = create_info.set_layouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = create_info.push_constant_size > 0 ? 1 : 0;
    pipeline_layout_create_info.pPushConstantRanges = &push_range;

    pipeline result_pipeline;

    VkResult result = disp.createPipelineLayout(&pipeline_layout_create_info, nullptr, &result_pipeline.layout);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create pipeline layout");

    const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_create_info = {};
    dynamic_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_create_info.dynamicStateCount = 2;
    dynamic_create_info.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_create_info = {};
    rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_create_info.colorAttachmentCount = color_count;
    rendering_create_info.pColorAttachmentFormats = color_count > 0 ? create_info.color_formats.data() : nullptr;
    rendering_create_info.depthAttachmentFormat = create_info.depth_format;
    rendering_create_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.pNext = &rendering_create_info;
    pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_create_info.pStages = shader_stages.data();
    pipeline_create_info.pVertexInputState = &vertex_input_create_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = create_info.depth_format != VK_FORMAT_UNDEFINED ? &depth_stencil : nullptr;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_create_info;
    pipeline_create_info.layout = result_pipeline.layout;
    pipeline_create_info.renderPass = VK_NULL_HANDLE;
    pipeline_create_info.subpass = 0;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

    result = disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &result_pipeline.handle);

    if (frag_module != VK_NULL_HANDLE) {
        destroy_module(frag_module);
    }

    destroy_module(vert_module);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create graphics pipeline");

    return result_pipeline;
}

pipeline create_compute_pipeline(const std::string& comp_file, const compute_pipeline_create_info& create_info)
{
    const vkb::DispatchTable& disp = device::get_disp();

    const std::string comp_source = utilities::file::read_raw(std::string(SHADERS_DIR) + comp_file);

    VkShaderModule comp_module = create_module(compile_glsl(comp_source, shader_stage::compute, comp_file, create_info.defines));

    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = create_info.push_constant_size;

    VkPipelineLayoutCreateInfo layout_create_info = {};
    layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_create_info.setLayoutCount = static_cast<uint32_t>(create_info.set_layouts.size());
    layout_create_info.pSetLayouts = create_info.set_layouts.data();
    layout_create_info.pushConstantRangeCount = create_info.push_constant_size > 0 ? 1 : 0;
    layout_create_info.pPushConstantRanges = &push_range;

    pipeline result_pipeline;

    VkResult result = disp.createPipelineLayout(&layout_create_info, nullptr, &result_pipeline.layout);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create compute pipeline layout");

    VkPipelineShaderStageCreateInfo stage_create_info = {};
    stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_create_info.module = comp_module;
    stage_create_info.pName = "main";

    VkComputePipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create_info.stage = stage_create_info;
    pipeline_create_info.layout = result_pipeline.layout;

    result = disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &result_pipeline.handle);

    destroy_module(comp_module);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create compute pipeline");

    return result_pipeline;
}

void destroy_pipeline(const pipeline& target)
{
    device::get_disp().destroyPipeline(target.handle, nullptr);
    device::get_disp().destroyPipelineLayout(target.layout, nullptr);
}

}
