#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/ui/file_field.h"

#include "glp/glaze/scene/objects/static_object.h"
#include "glp/glaze/scene/volume/cmp_model.h"

#include <exception>
#include <filesystem>
#include <string>

namespace trainer::objects
{

namespace
{

namespace component = glaze::scene::vol::component;

constexpr const char* MODEL_FILTERS = "Models{.gltf,.glb}";

std::string m_model_draft;

std::string to_lower(std::string text)
{
    for (char& symbol : text) {
        symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
    }

    return text;
}

bool is_model_path(const std::string& path)
{
    std::error_code error;

    const std::string extension = to_lower(std::filesystem::path(path).extension().string());

    return std::filesystem::is_regular_file(path, error) && (extension == ".gltf" || extension == ".glb");
}

void draw_model_fields(glaze::scene::scene_entity* entity)
{
    component::model* target = find_component<component::model>(entity);

    if (target == nullptr) {
        return;
    }

    std::string path = target->get_path();

    if (!ui::file_field::draw("model", path, MODEL_FILTERS, ui::file_field::pick_mode::file) || path == target->get_path()) {
        return;
    }

    if (!is_model_path(path)) {
        report_error("model file not found: " + path);
        return;
    }

    try {
        target->set_path(path);
    }

    catch (const std::exception& error) {
        report_error(error.what());
    }
}

glaze::scene::scene_entity* create_model(glm::vec3 position)
{
    if (!is_model_path(m_model_draft)) {
        report_error("model file not found: " + m_model_draft);
        return nullptr;
    }

    try {
        return glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(
            position, glm::vec3(0.0f), glm::vec3(1.0f), m_model_draft
        );
    }

    catch (const std::exception& error) {
        report_error(error.what());
        return nullptr;
    }
}

const bool registered = [] {
    object_type type;
    type.name         = "static_object_model_prim";
    type.label        = "Static Mesh";
    type.is_creatable = true;
    type.matches      = is_type<glaze::scene::obj::static_object_model_prim>;
    type.draw_fields  = draw_model_fields;
    type.draw_draft   = [] { ui::file_field::draw("model", m_model_draft, MODEL_FILTERS, ui::file_field::pick_mode::file); };
    type.create       = create_model;

    register_type(type);

    return true;
}();

}

}

#endif
