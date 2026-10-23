#include "glp/trainer/objects/field_editors.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/scene/objects/post_object.h"
#include "glp/glaze/scene/volume/cmp_post.h"

#include <array>
#include <cfloat>
#include <cstdint>

namespace trainer::objects
{

namespace
{

namespace component = glaze::scene::vol::component;

constexpr float SPEED_FINE   = 0.001f;
constexpr float SPEED_SMALL  = 0.01f;
constexpr float SPEED_LARGE  = 1.0f;

constexpr uint32_t MAX_GTAO_DIRECTIONS = 16;
constexpr uint32_t MAX_GTAO_STEPS      = 32;

const std::array<const char*, 3> TONEMAPPERS = { "pbr neutral", "aces", "agx" };
const std::array<const char*, 5> UPSCALE_MODES = { "native aa", "quality", "balanced", "performance", "ultra performance" };

component::post_create_info m_post_draft;

const std::vector<info_field<component::post_create_info>>& post_fields()
{
    static const std::vector<info_field<component::post_create_info>> fields = {
        INFO_FIELD(ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD_IN("exposure", auto_exposure, bool_editor()),
        INFO_FIELD_IN("exposure", exposure_compensation, float_editor(SPEED_SMALL)),
        INFO_FIELD_IN("exposure", min_ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD_IN("exposure", max_ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD_IN("exposure", exposure_speed_up, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("exposure", exposure_speed_down, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("exposure", exposure_night_darkening, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("exposure", exposure_center_weight, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("exposure", exposure_incident_metering, bool_editor()),
        INFO_FIELD_IN("tonemap", tonemapper, enum_editor(TONEMAPPERS)),
        INFO_FIELD_IN("tonemap", tonemap_contrast, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("tonemap", tonemap_saturation, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("post", sharpen, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("post", bloom_intensity, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("post", light_shafts_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("post", sun_glare_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("post", night_vision_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("gtao", gtao_radius, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("gtao", gtao_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("gtao", gtao_directions, uint_editor(1, MAX_GTAO_DIRECTIONS)),
        INFO_FIELD_IN("gtao", gtao_steps, uint_editor(1, MAX_GTAO_STEPS)),
        INFO_FIELD_IN("scaling", upscale_mode, enum_editor(UPSCALE_MODES)),
        INFO_FIELD_IN("scaling", gpu_budget_fraction, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("shadows", shadows_enabled, bool_editor()),
        INFO_FIELD_IN("shadows", shadow_lod_bias, float_editor(SPEED_SMALL)),
        INFO_FIELD_IN("shadows", shadow_softness, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("shadows", shadow_normal_bias, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("shadows", contact_shadow_length, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", volumetric_fog_enabled, bool_editor()),
        INFO_FIELD_IN("fog", volumetric_fog_distance, float_editor(SPEED_LARGE, 0.0f, FLT_MAX))
    };

    return fields;
}

void draw_post_fields(glaze::scene::scene_entity* entity)
{
    component::post* target = find_component<component::post>(entity);

    if (target == nullptr) {
        return;
    }

    component::post_create_info info = target->get_create_info();

    if (edit_fields(post_fields(), info)) {
        target->set_create_info(info);
    }
}

const bool registered = [] {
    object_type type;
    type.name              = "post_object_prim";
    type.label             = "Post Process";
    type.has_transform     = false;
    type.is_creatable      = true;
    type.only_first_active = true;
    type.matches           = is_type<glaze::scene::obj::post_object_prim>;
    type.draw_fields       = draw_post_fields;
    type.draw_draft        = [] { edit_fields(post_fields(), m_post_draft); };
    type.create            = [](glm::vec3) -> glaze::scene::scene_entity* {
        return glaze::scene::it::create_entity<glaze::scene::obj::post_object_prim>(m_post_draft);
    };

    register_type(type);

    return true;
}();

}

}

#endif
