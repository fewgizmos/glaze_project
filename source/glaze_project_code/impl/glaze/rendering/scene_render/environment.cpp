#include "glp/glaze/rendering/scene_render/environment.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

namespace glaze::rendering::scene_render::environment
{

namespace
{

constexpr uint32_t BRDF_LUT_SIZE  = 256;
constexpr uint32_t BRDF_LUT_GROUP = 8;

constexpr uint32_t CUBE_FACES = 6;

constexpr VkFormat BRDF_LUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat BLACK_FORMAT    = VK_FORMAT_R16G16B16A16_SFLOAT;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_default_set = VK_NULL_HANDLE;

image m_brdf_lut;
image m_black_cube;

VkSampler m_cube_sampler = VK_NULL_HANDLE;
VkSampler m_lut_sampler  = VK_NULL_HANDLE;

void create_set_layout()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { SKY_BINDING,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { PREFILTERED_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { BRDF_LUT_BINDING,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
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
        BLACK_FORMAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        CUBE_FACES,
        true
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

        const uint32_t groups = (BRDF_LUT_SIZE + BRDF_LUT_GROUP - 1) / BRDF_LUT_GROUP;

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

        vk::commands::transition_image(
            cmd,
            m_black_cube.handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            CUBE_FACES
        );

        const VkClearColorValue black = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = CUBE_FACES;

        disp.cmdClearColorImage(cmd, m_black_cube.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

        vk::commands::transition_image(
            cmd,
            m_black_cube.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            CUBE_FACES
        );
    });

    vk::descriptors::destroy_pool(bake_pool);
    vk::descriptors::destroy_set_layout(bake_layout);
}

void create_default_set()
{
    m_pool        = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 } });
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
}

}

void init_all()
{
    create_set_layout();

    create_samplers();

    create_images();

    bake_images();

    create_default_set();
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    vk::resources::destroy_image(m_black_cube);
    vk::resources::destroy_image(m_brdf_lut);

    m_set_layout  = VK_NULL_HANDLE;
    m_pool        = VK_NULL_HANDLE;
    m_default_set = VK_NULL_HANDLE;

    m_cube_sampler = VK_NULL_HANDLE;
    m_lut_sampler  = VK_NULL_HANDLE;
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

}
