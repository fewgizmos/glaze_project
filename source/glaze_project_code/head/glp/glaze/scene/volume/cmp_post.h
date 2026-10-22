#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/fsr.h"

#include <cstdint>

namespace glaze::scene::vol::component
{

enum class tonemap_operator : uint32_t
{
    pbr_neutral = 0,
    aces        = 1,
    agx         = 2
};

struct post_create_info
{
    float ev100 = 15.0f;

    bool auto_exposure = true;

    float exposure_compensation = 1.0f;
    float min_ev100             = -6.0f;
    float max_ev100             = 18.0f;
    float exposure_speed_up     = 3.0f;
    float exposure_speed_down   = 1.0f;

    float exposure_night_darkening = 0.6f;
    float exposure_center_weight   = 8.0f;

    bool exposure_incident_metering = true;

    float gpu_budget_fraction = 0.75f;

    tonemap_operator tonemapper = tonemap_operator::agx;

    float tonemap_contrast   = 1.2f;
    float tonemap_saturation = 1.2f;

    float sharpen = 0.5f;

    float bloom_intensity = 0.04f;

    float light_shafts_intensity = 1.0f;
    float sun_glare_intensity    = 1.0f;

    float night_vision_intensity = 1.0f;

    float gtao_radius    = 1.0f;
    float gtao_intensity = 1.0f;

    uint32_t gtao_directions = 4;
    uint32_t gtao_steps      = 6;

    rendering::scene_render::fsr::upscale_mode upscale_mode = rendering::scene_render::fsr::upscale_mode::balanced;

    bool shadows_enabled = true;

    float shadow_lod_bias    = 0.0f;
    float shadow_softness    = 1.0f;
    float shadow_normal_bias = 1.5f;

    float contact_shadow_length = 0.25f;

    bool volumetric_fog_enabled = true;

    float volumetric_fog_distance = 100.0f;
};

class post : public volume_component
{
public:
    post(const post_create_info& create_info);

    rendering::scene_render::gpu_post get_post() override;

    const post_create_info& get_create_info() const { return m_create_info; }
    void set_create_info(const post_create_info& create_info) { m_create_info = create_info; }

private:
    post_create_info m_create_info;
};

}
