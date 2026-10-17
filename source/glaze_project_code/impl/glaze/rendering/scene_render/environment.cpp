#include "glp/glaze/rendering/scene_render/environment.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <algorithm>

namespace glaze::rendering::scene_render::environment
{

namespace
{

struct prefilter_push
{
    float roughness;
    float source_resolution;
};

struct irradiance_sh_push
{
    VkDeviceAddress output;
};

constexpr uint32_t BRDF_LUT_SIZE = 256;

constexpr VkFormat BRDF_LUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat DEFAULT_FORMAT  = VK_FORMAT_R16G16B16A16_SFLOAT;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout m_bake_set_layout = VK_NULL_HANDLE;

VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_default_set = VK_NULL_HANDLE;

image m_brdf_lut;
image m_black_cube;
image m_default_aerial;

VkSampler m_cube_sampler = VK_NULL_HANDLE;
VkSampler m_lut_sampler  = VK_NULL_HANDLE;

const shader* m_prefilter_shader     = nullptr;
const shader* m_irradiance_sh_shader = nullptr;

void create_set_layouts()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { SKY_BINDING,                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { PREFILTERED_BINDING,             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { BRDF_LUT_BINDING,                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { AERIAL_PERSPECTIVE_BINDING,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { MOON_AERIAL_PERSPECTIVE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    m_bake_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });
}

void create_samplers()
{
    sampler_create_info cube_info;
    cube_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_info.anisotropy = false;

    m_cube_sampler = vk::samplers::get(vk::samplers::make(cube_info));

    sampler_create_info lut_info = cube_info;
    lut_info.mip_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    m_lut_sampler = vk::samplers::get(vk::samplers::make(lut_info));
}

void create_shaders()
{
    compute_pipeline_create_info prefilter_info;
    prefilter_info.set_layouts        = { m_bake_set_layout };
    prefilter_info.push_constant_size = sizeof(prefilter_push);

    m_prefilter_shader = res::shaders::build("ibl_prefilter", "", "", "sys/ibl_prefilter.comp", {}, prefilter_info, render_queue::opaque);

    compute_pipeline_create_info irradiance_sh_info;
    irradiance_sh_info.set_layouts        = { m_bake_set_layout };
    irradiance_sh_info.push_constant_size = sizeof(irradiance_sh_push);

    m_irradiance_sh_shader = res::shaders::build("ibl_irradiance_sh", "", "", "sys/ibl_irradiance_sh.comp", {}, irradiance_sh_info, render_queue::opaque);
}

void create_images()
{
    m_brdf_lut = vk::resources::create_image(
        { BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1 },
        BRDF_LUT_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    m_black_cube = vk::resources::create_image(
        { 1, 1, 1 },
        DEFAULT_FORMAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        CUBE_FACES,
        true
    );

    m_default_aerial = vk::resources::create_volume_image(
        { 1, 1, 1 },
        DEFAULT_FORMAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );
}

void clear_default_image(VkCommandBuffer cmd, const image& target, uint32_t layers, const VkClearColorValue& value)
{
    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,
        0,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        layers
    );

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = layers;

    vk::device::get_disp().cmdClearColorImage(cmd, target.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);

    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        layers
    );
}

void bake_images()
{
    const VkDescriptorSetLayout bake_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    const VkDescriptorPool bake_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 } });
    const VkDescriptorSet bake_set   = vk::descriptors::allocate_set(bake_pool, bake_layout);

    vk::descriptors::write_image(bake_set, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_brdf_lut.view, VK_IMAGE_LAYOUT_GENERAL);

    compute_pipeline_create_info compute_info;
    compute_info.set_layouts = { bake_layout };

    const shader* lut_shader = res::shaders::build("ibl_brdf_lut", "", "", "sys/ibl_brdf_lut.comp", {}, compute_info, render_queue::opaque);

    vk::commands::immediate_submit([&](VkCommandBuffer cmd) {
        const vkb::DispatchTable& disp = vk::device::get_disp();

        vk::commands::transition_image(
            cmd,
            m_brdf_lut.handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
        );

        disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lut_shader->compute.layout, 0, 1, &bake_set, 0, nullptr);

        const uint32_t groups = get_bake_groups(BRDF_LUT_SIZE);

        vk::commands::dispatch(cmd, lut_shader->compute, nullptr, 0, groups, groups, 1);

        vk::commands::transition_image(
            cmd,
            m_brdf_lut.handle,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );

        clear_default_image(cmd, m_black_cube, CUBE_FACES, { { 0.0f, 0.0f, 0.0f, 1.0f } });
        clear_default_image(cmd, m_default_aerial, 1, { { 0.0f, 0.0f, 0.0f, 1.0f } });
    });

    vk::descriptors::destroy_pool(bake_pool);
    vk::descriptors::destroy_set_layout(bake_layout);
}

void create_default_set()
{
    m_pool        = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SET_SAMPLERS_COUNT } });
    m_default_set = vk::descriptors::allocate_set(m_pool, m_set_layout);

    for (const uint32_t binding : { SKY_BINDING, PREFILTERED_BINDING }) {
        vk::descriptors::write_image(
            m_default_set,
            binding,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            m_black_cube.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_cube_sampler
        );
    }

    write_brdf_lut(m_default_set);
    write_default_aerial_perspective(m_default_set);
}

}

void init_all()
{
    create_set_layouts();

    create_samplers();

    create_shaders();

    create_images();

    bake_images();

    create_default_set();
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_bake_set_layout);
    vk::descriptors::destroy_set_layout(m_set_layout);

    vk::resources::destroy_image(m_default_aerial);
    vk::resources::destroy_image(m_black_cube);
    vk::resources::destroy_image(m_brdf_lut);

    m_set_layout      = VK_NULL_HANDLE;
    m_bake_set_layout = VK_NULL_HANDLE;
    m_pool            = VK_NULL_HANDLE;
    m_default_set     = VK_NULL_HANDLE;

    m_cube_sampler = VK_NULL_HANDLE;
    m_lut_sampler  = VK_NULL_HANDLE;

    m_prefilter_shader     = nullptr;
    m_irradiance_sh_shader = nullptr;
}

VkDescriptorSetLayout get_set_layout() { return m_set_layout; }
VkDescriptorSet get_default_set()      { return m_default_set; }

VkSampler get_cube_sampler() { return m_cube_sampler; }

void write_brdf_lut(VkDescriptorSet set)
{
    vk::descriptors::write_image(
        set,
        BRDF_LUT_BINDING,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        m_brdf_lut.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_lut_sampler
    );
}

void write_default_aerial_perspective(VkDescriptorSet set)
{
    for (const uint32_t binding : { AERIAL_PERSPECTIVE_BINDING, MOON_AERIAL_PERSPECTIVE_BINDING }) {
        vk::descriptors::write_image(
            set,
            binding,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            m_default_aerial.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_lut_sampler
        );
    }
}

VkDescriptorSetLayout get_bake_set_layout() { return m_bake_set_layout; }

void write_bake_set(VkDescriptorSet set, const image& source, VkImageView target)
{
    vk::descriptors::write_image(
        set,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        source.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_cube_sampler
    );

    if (target != VK_NULL_HANDLE) {
        vk::descriptors::write_image(set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target, VK_IMAGE_LAYOUT_GENERAL);
    }
}

uint32_t get_bake_groups(uint32_t size)
{
    return (size + BAKE_GROUP - 1) / BAKE_GROUP;
}

void transition_bake_target(VkCommandBuffer cmd, const image& target, VkImageLayout old_layout, VkImageLayout new_layout)
{
    const bool to_general = new_layout == VK_IMAGE_LAYOUT_GENERAL;

    vk::commands::transition_image(
        cmd,
        target.handle,
        old_layout,
        new_layout,
        to_general ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        to_general ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        to_general ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        to_general ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        target.mip_levels,
        target.array_layers
    );
}

void record_prefilter(VkCommandBuffer cmd, const image& prefiltered, const VkDescriptorSet* mip_sets, float source_resolution)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    transition_bake_target(cmd, prefiltered, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    for (uint32_t mip = 0; mip < PREFILTERED_MIPS; mip++) {
        const uint32_t mip_size = std::max(PREFILTERED_SIZE >> mip, 1u);

        const prefilter_push push = {
            static_cast<float>(mip) / static_cast<float>(PREFILTERED_MIPS - 1),
            source_resolution
        };

        disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilter_shader->compute.layout, 0, 1, &mip_sets[mip], 0, nullptr);

        vk::commands::dispatch(
            cmd,
            m_prefilter_shader->compute,
            &push,
            sizeof(push),
            get_bake_groups(mip_size),
            get_bake_groups(mip_size),
            CUBE_FACES
        );
    }

    transition_bake_target(cmd, prefiltered, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void record_irradiance_sh(VkCommandBuffer cmd, VkDescriptorSet source_set, VkDeviceAddress output)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    const irradiance_sh_push push = { output };

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiance_sh_shader->compute.layout, 0, 1, &source_set, 0, nullptr);

    vk::commands::dispatch(cmd, m_irradiance_sh_shader->compute, &push, sizeof(push), 1, 1, 1);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}

}
