#include <vector>
#include <string.h>
#include <tinygltf/stb_image.h>
#include <ac/ac.h>
#include "model.hpp"
#include "pbr_maps.hpp"

#include "compiled/main.h"

#define MAX_IMAGES 500
#define PBR_WORKFLOW_METALLIC_ROUGHNESS 0
#define PBR_WORKFLOW_SPECULAR_GLOSINESS 1

#define RIF(x)                                                                 \
  do                                                                           \
  {                                                                            \
    ac_result res = (x);                                                       \
    if (res != ac_result_success)                                              \
    {                                                                          \
      return;                                                                  \
    }                                                                          \
  }                                                                            \
  while (false)

class App {
private:
  static constexpr const char* APP_NAME = "05_pbr";

  enum Token : uint64_t {
    ColorImage = 0,
    DepthImage = 1,
  };

  struct ShaderMaterial {
    glm::vec4 base_color_factor;
    glm::vec4 emissive_factor;
    glm::vec4 diffuse_factor;
    glm::vec4 specular_factor;
    float     workflow;
    int32_t   base_color_index;
    int32_t   metallic_roughness_index;
    int32_t   normal_index;
    int32_t   occlusion_index;
    int32_t   emissive_index;
    float     metallic_factor;
    float     roughness_factor;
    float     alpha_mask;
    float     alpha_mask_cutoff;
    float     emissive_strength;
    float     pad;
  };

  bool m_running = {};
  bool m_window_changed = {};

  ac_wsi m_wsi = {};

  ac_device    m_device = {};
  ac_swapchain m_swapchain = {};
  ac_fence     m_acquire_finished_fences[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_fence     m_render_finished_fences[AC_MAX_FRAME_IN_FLIGHT] = {};
  uint32_t     m_frame_index = {};

  ac_rg       m_rg = {};
  ac_rg_graph m_graph = {};

  ac_dsl               m_dsl = {};
  ac_descriptor_buffer m_db = {};

  struct {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 cam_pos;
  } m_camera = {};

  struct {
    ac_pipeline pbr;
    ac_pipeline pbr_double_sided;
    ac_pipeline pbr_alpha_blended;
  } m_pipelines = {};

  ac_shader m_vertex_shader = {};
  ac_shader m_fragment_shader = {};

  PBRMaps m_maps = {};

  ac_buffer  m_camera_buffers[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_buffer  m_material_buffer = {};
  ac_sampler m_sampler = {};
  ac_image   m_stub_image = {};

  uint32_t m_animation_index = {};
  float    m_dt = {};
  float    m_animation_timer = {};

  Model m_scene = {};

  static void
  window_callback(const ac_window_event* event, void* ud);

  static ac_result
  build_frame(ac_rg_builder builder, void* ud);
  static ac_result
  stage_prepare(ac_rg_stage* stage, void* ud);
  static ac_result
  stage_cmd(ac_rg_stage* stage, void* ud);

  ac_result
  create_window_dependents();

  ac_result
  create_stub_images();

  void
  render_node(ac_rg_stage* stage, Node* node, Material::AlphaMode alpha_mode);

public:
  App();
  ~App();

  ac_result
  run();
};

App::App()
{
  {
    ac_init_info info = {};
    info.app_name = App::APP_NAME;
    info.enable_memory_manager = AC_INCLUDE_DEBUG;
    RIF(ac_init(&info));
  }
  RIF(ac_init_window(App::APP_NAME));

  {
    m_wsi.native_window = ac_window_get_native_handle();

    m_wsi.get_vk_instance_extensions =
      [](void* ud, uint32_t* count, const char** names) -> ac_result
    {
      AC_UNUSED(ud);
      return ac_window_get_vk_instance_extensions(count, names);
    };
    m_wsi.create_vk_surface =
      [](void* ud, void* instance, void** surface) -> ac_result
    {
      AC_UNUSED(ud);
      return ac_window_create_vk_surface(instance, surface);
    };
  }

  {
    ac_window_state state = {};
    state.callback = App::window_callback;
    state.callback_data = this;
    RIF(ac_window_set_state(&state));
  }

  {
    ac_device_info info = {};
    info.debug_bits = ac_device_debug_validation_bit;
    info.wsi = &m_wsi;
    RIF(ac_create_device(&info, &m_device));
  }

  for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
  {
    ac_fence_info info = {};
    info.bits = ac_fence_present_bit;
    RIF(ac_create_fence(m_device, &info, &m_acquire_finished_fences[i]));
    RIF(ac_create_fence(m_device, &info, &m_render_finished_fences[i]));
  }

  RIF(ac_create_rg(m_device, &m_rg));

  {
    ac_rg_graph_info info = {};
    info.name = AC_DEBUG_NAME(App::APP_NAME);
    info.user_data = this;
    info.cb_build = App::build_frame;

    RIF(ac_rg_create_graph(m_rg, &info, &m_graph));
  }

  RIF(compute_pbr_maps(m_device, "clouds.hdr", &m_maps));
  RIF(create_stub_images());

  m_scene.load_from_file(
    "BrainStem.glb",
    m_device,
    ac_device_get_queue(m_device, ac_queue_type_graphics));

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_vertex;
    info.code = main_vs[0];

    RIF(ac_create_shader(m_device, &info, &m_vertex_shader));
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_pixel;
    info.code = main_fs[0];

    RIF(ac_create_shader(m_device, &info, &m_fragment_shader));
  }

  {
    ac_shader shaders[] = {
      m_vertex_shader,
      m_fragment_shader,
    };
    ac_dsl_info info = {};
    info.shader_count = AC_COUNTOF(shaders);
    info.shaders = shaders;
    RIF(ac_create_dsl(m_device, &info, &m_dsl));
  }

  {
    ac_descriptor_buffer_info info = {};
    info.dsl = m_dsl;
    info.max_sets[ac_space0] = AC_MAX_FRAME_IN_FLIGHT;
    info.max_sets[ac_space1] = 1;
    info.max_sets[ac_space2] = 1;
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
  }

  {
    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_buffer_info info = {};
      info.size = sizeof(m_camera);
      info.usage = ac_buffer_usage_cbv_bit;
      info.memory_usage = ac_memory_usage_cpu_to_gpu;
      info.name = AC_DEBUG_NAME("camera buffer");

      RIF(ac_create_buffer(m_device, &info, &m_camera_buffers[i]));
      RIF(ac_buffer_map_memory(m_camera_buffers[i]));
    }
  }

  {
    ac_buffer_info info = {};
    info.size = sizeof(ShaderMaterial) * m_scene.materials.size();
    info.usage = ac_buffer_usage_srv_bit;
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.name = AC_DEBUG_NAME("material buffer");

    RIF(ac_create_buffer(m_device, &info, &m_material_buffer));
    RIF(ac_buffer_map_memory(m_material_buffer));

    for (uint32_t i = 0; i < m_scene.materials.size(); ++i)
    {
      const Material* in = &m_scene.materials[i];
      ShaderMaterial* out =
        ((ShaderMaterial*)ac_buffer_get_mapped_memory(m_material_buffer));

      out += i;

      out->emissive_factor = in->emissive_factor;
      out->base_color_index =
        in->base_color_texture != nullptr ? in->tex_coord_sets.base_color : -1;
      out->normal_index =
        in->normal_texture != nullptr ? in->tex_coord_sets.normal : -1;
      out->occlusion_index =
        in->occlusion_texture != nullptr ? in->tex_coord_sets.occlusion : -1;
      out->emissive_index =
        in->emissive_texture != nullptr ? in->tex_coord_sets.emissive : -1;
      out->alpha_mask =
        static_cast<float>(in->alpha_mode == Material::ALPHAMODE_MASK);
      out->alpha_mask_cutoff = in->alpha_cutoff;
      out->emissive_strength = in->emissive_strength;

      if (in->pbr_workflows.metallic_roughness)
      {
        out->workflow = PBR_WORKFLOW_METALLIC_ROUGHNESS;
        out->base_color_factor = in->base_color_factor;
        out->metallic_factor = in->metallic_factor;
        out->roughness_factor = in->roughness_factor;
        out->metallic_roughness_index =
          in->metallic_roughness_texture != nullptr
            ? in->tex_coord_sets.metallic_roughness
            : -1;
        out->base_color_index = in->base_color_texture != nullptr
                                  ? in->tex_coord_sets.base_color
                                  : -1;
      }

      if (in->pbr_workflows.specular_glossiness)
      {
        out->workflow = PBR_WORKFLOW_SPECULAR_GLOSINESS;
        out->metallic_roughness_index =
          in->extension.specular_glossiness_texture != nullptr
            ? in->tex_coord_sets.specular_glossiness
            : -1;
        out->base_color_index = in->extension.diffuse_texture != nullptr
                                  ? in->tex_coord_sets.base_color
                                  : -1;
        out->diffuse_factor = in->extension.diffuse_factor;
        out->specular_factor = glm::vec4(in->extension.specular_factor, 1.0f);
      }
    }

    ac_buffer_unmap_memory(m_material_buffer);
  }

  {
    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_descriptor camera_descriptor = {};
      camera_descriptor.buffer = m_camera_buffers[i];

      ac_descriptor_write write = {};
      write.type = ac_descriptor_type_cbv_buffer;
      write.count = 1;
      write.descriptors = &camera_descriptor;

      ac_update_set(m_db, ac_space0, i, 1, &write);
    }

    {
      ac_descriptor d = {};
      d.buffer = this->m_scene.matrices;

      ac_descriptor_write write = {};
      write.type = ac_descriptor_type_srv_buffer;
      write.count = 1;
      write.descriptors = &d;

      ac_update_set(m_db, ac_space1, 0, 1, &write);
    }
  }

  {
    ac_sampler_info sampler_info = {};
    sampler_info.min_filter = ac_filter_linear;
    sampler_info.mag_filter = ac_filter_linear;
    sampler_info.mipmap_mode = ac_sampler_mipmap_mode_linear;
    sampler_info.address_mode_u = ac_sampler_address_mode_repeat;
    sampler_info.address_mode_v = ac_sampler_address_mode_repeat;
    sampler_info.address_mode_w = ac_sampler_address_mode_repeat;
    sampler_info.anisotropy_enable = true;
    sampler_info.max_anisotropy = 16;
    sampler_info.min_lod = 0;
    sampler_info.max_lod = 1000;

    RIF(ac_create_sampler(m_device, &sampler_info, &m_sampler));

    ac_descriptor sampler_descriptor = {};
    sampler_descriptor.sampler = m_sampler;

    ac_descriptor buffer_descriptor = {};
    buffer_descriptor.buffer = m_material_buffer;

    ac_descriptor image_descriptors[MAX_IMAGES] = {};
    ac_descriptor stub_descriptor = {};
    stub_descriptor.image = m_stub_image;
    std::fill(
      image_descriptors,
      image_descriptors + MAX_IMAGES,
      stub_descriptor);

    for (uint32_t i = 0; i < m_scene.textures.size(); ++i)
    {
      image_descriptors[i].image = m_scene.textures[i].image;
    }

    ac_descriptor irradiance_descriptor = {};
    irradiance_descriptor.image = m_maps.irradiance;

    ac_descriptor specular_descriptor = {};
    specular_descriptor.image = m_maps.specular;

    ac_descriptor brdf_descriptor = {};
    brdf_descriptor.image = m_maps.brdf;

    ac_descriptor_write writes[6] = {};
    writes[0].type = ac_descriptor_type_sampler;
    writes[0].count = 1;
    writes[0].descriptors = &sampler_descriptor;

    writes[1].type = ac_descriptor_type_srv_buffer;
    writes[1].count = 1;
    writes[1].descriptors = &buffer_descriptor;

    writes[2].type = ac_descriptor_type_srv_image;
    writes[2].count = 1;
    writes[2].descriptors = &irradiance_descriptor;
    writes[2].reg = 1;

    writes[3].type = ac_descriptor_type_srv_image;
    writes[3].count = 1;
    writes[3].descriptors = &specular_descriptor;
    writes[3].reg = 2;

    writes[4].type = ac_descriptor_type_srv_image;
    writes[4].count = 1;
    writes[4].descriptors = &brdf_descriptor;
    writes[4].reg = 3;

    writes[5].type = ac_descriptor_type_srv_image;
    writes[5].count = AC_COUNTOF(image_descriptors);
    writes[5].descriptors = image_descriptors;
    writes[5].reg = 4;

    ac_update_set(m_db, ac_space2, 0, AC_COUNTOF(writes), writes);
  }

  {
    glm::vec3 eye = {0.0, 3.0f, 5.0f};
    glm::vec3 origin = {0.0f, 0.0f, 0.0f};
    glm::vec3 up = {0.0f, 1.0f, 0.0f};

    ac_window_state state = ac_window_get_state();

    m_camera.projection = glm::perspective(
      glm::radians(45.0f),
      (float)state.width / (float)state.height,
      0.1f,
      100.0f);

    m_camera.view = glm::lookAt(eye, origin, up);
    m_camera.model = glm::identity<glm::mat4>();
    m_camera.cam_pos = glm::vec4(eye, 0.0);
  }

  RIF(create_window_dependents());

  m_running = true;
}

App::~App()
{
  if (m_device)
  {
    RIF(ac_queue_wait_idle(
      ac_device_get_queue(m_device, ac_queue_type_graphics)));

    m_scene.destroy(m_device);

    ac_destroy_image(m_maps.environment);
    ac_destroy_image(m_maps.irradiance);
    ac_destroy_image(m_maps.specular);
    ac_destroy_image(m_maps.brdf);

    ac_destroy_image(m_stub_image);
    ac_destroy_sampler(m_sampler);
    ac_destroy_buffer(m_material_buffer);

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_buffer(m_camera_buffers[i]);
    }

    ac_destroy_descriptor_buffer(m_db);
    ac_destroy_pipeline(m_pipelines.pbr);
    ac_destroy_pipeline(m_pipelines.pbr_alpha_blended);
    ac_destroy_pipeline(m_pipelines.pbr_double_sided);
    ac_destroy_dsl(m_dsl);
    ac_destroy_shader(m_vertex_shader);
    ac_destroy_shader(m_fragment_shader);

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_fence(m_render_finished_fences[i]);
      ac_destroy_fence(m_acquire_finished_fences[i]);
    }

    ac_rg_destroy_graph(m_graph);
    ac_destroy_rg(m_rg);

    ac_queue_wait_idle(ac_device_get_queue(m_device, ac_queue_type_graphics));
    ac_destroy_swapchain(m_swapchain);

    ac_destroy_device(m_device);
    m_device = NULL;
  }

  ac_shutdown_window();
  ac_shutdown();
}

ac_result
App::run()
{
  if (!m_running)
  {
    return ac_result_unknown_error;
  }

  uint64_t prev_time = ac_get_time(ac_time_unit_milliseconds);

  while (m_running)
  {
    uint64_t current_time = ac_get_time(ac_time_unit_milliseconds);
    m_dt = ((float)(current_time - prev_time)) / 1000.0f;
    prev_time = current_time;

    ac_window_poll_events();

    if (m_window_changed)
    {
      if (create_window_dependents() != ac_result_success)
      {
        continue;
      }
      m_window_changed = false;
    }

    ac_result res;

    res = ac_acquire_next_image(
      m_swapchain,
      m_acquire_finished_fences[m_frame_index]);

    if (res != ac_result_success)
    {
      m_window_changed = true;
      continue;
    }

    res = ac_rg_graph_execute(m_graph);

    if (res != ac_result_success)
    {
      m_window_changed = true;
      continue;
    }

    ac_queue_present_info queue_present_info = {};
    queue_present_info.wait_fence_count = 1;
    queue_present_info.wait_fences = &m_render_finished_fences[m_frame_index];
    queue_present_info.swapchain = m_swapchain;

    res = ac_queue_present(
      ac_device_get_queue(m_device, ac_queue_type_graphics),
      &queue_present_info);

    if (res != ac_result_success)
    {
      m_window_changed = true;
      continue;
    }

    m_frame_index = (m_frame_index + 1) % AC_MAX_FRAME_IN_FLIGHT;
  }

  return ac_result_success;
}

void
App::window_callback(const ac_window_event* event, void* ud)
{
  App* p = static_cast<App*>(ud);

  switch (event->type)
  {
  case ac_window_event_type_monitor_change:
  case ac_window_event_type_resize:
  {
    p->m_window_changed = true;
    break;
  }
  case ac_window_event_type_close:
  {
    p->m_running = false;
    break;
  }
  default:
  {
    break;
  }
  }
}

ac_result
App::create_window_dependents()
{
  ac_queue_wait_idle(ac_device_get_queue(m_device, ac_queue_type_graphics));
  ac_destroy_swapchain(m_swapchain);

  ac_window_state state = ac_window_get_state();

  if (!state.width || !state.height)
  {
    return ac_result_not_ready;
  }

  ac_swapchain_info swapchain_info = {};
  swapchain_info.width = state.width;
  swapchain_info.height = state.height;

  swapchain_info.min_image_count = AC_MAX_FRAME_IN_FLIGHT;
  swapchain_info.vsync = true;
  swapchain_info.queue = ac_device_get_queue(m_device, ac_queue_type_graphics);
  swapchain_info.wsi = &m_wsi;

  AC_RIF(ac_create_swapchain(m_device, &swapchain_info, &m_swapchain));

  {
    ac_destroy_pipeline(m_pipelines.pbr);
    ac_destroy_pipeline(m_pipelines.pbr_alpha_blended);
    ac_destroy_pipeline(m_pipelines.pbr_double_sided);

    ac_vertex_layout layout = {};
    layout.binding_count = 1;
    layout.bindings[0].input_rate = ac_input_rate_vertex;
    layout.bindings[0].stride = sizeof(Model::Vertex);

    layout.attribute_count = 7;
    layout.attributes[0].format = ac_format_r32g32b32_sfloat;
    layout.attributes[0].semantic = ac_attribute_semantic_position;
    layout.attributes[0].offset = AC_OFFSETOF(Model::Vertex, pos);

    layout.attributes[1].format = ac_format_r32g32b32_sfloat;
    layout.attributes[1].semantic = ac_attribute_semantic_normal;
    layout.attributes[1].offset = AC_OFFSETOF(Model::Vertex, normal);

    layout.attributes[2].format = ac_format_r32g32_sfloat;
    layout.attributes[2].semantic = ac_attribute_semantic_texcoord0;
    layout.attributes[2].offset = AC_OFFSETOF(Model::Vertex, uv0);

    layout.attributes[3].format = ac_format_r32g32_sfloat;
    layout.attributes[3].semantic = ac_attribute_semantic_texcoord1;
    layout.attributes[3].offset = AC_OFFSETOF(Model::Vertex, uv1);

    layout.attributes[4].format = ac_format_r32g32b32a32_sfloat;
    layout.attributes[4].semantic = ac_attribute_semantic_texcoord2;
    layout.attributes[4].offset = AC_OFFSETOF(Model::Vertex, joint0);

    layout.attributes[5].format = ac_format_r32g32b32a32_sfloat;
    layout.attributes[5].semantic = ac_attribute_semantic_texcoord3;
    layout.attributes[5].offset = AC_OFFSETOF(Model::Vertex, weight0);

    layout.attributes[6].format = ac_format_r32g32b32a32_sfloat;
    layout.attributes[6].semantic = ac_attribute_semantic_color;
    layout.attributes[6].offset = AC_OFFSETOF(Model::Vertex, color);

    ac_depth_state_info depth = {};
    depth.depth_write = true;
    depth.depth_test = true;
    depth.compare_op = ac_compare_op_less;

    ac_rasterizer_state_info rasterizer = {};
    rasterizer.polygon_mode = ac_polygon_mode_fill;
    rasterizer.cull_mode = ac_cull_mode_back;
    rasterizer.front_face = ac_front_face_counter_clockwise;

    ac_image image = ac_swapchain_get_image(m_swapchain);

    ac_pipeline_info info = {};
    info.type = ac_pipeline_type_graphics;
    info.graphics.vertex_layout = layout;
    info.graphics.depth_state_info = depth;
    info.graphics.rasterizer_info = rasterizer;
    info.graphics.topology = ac_primitive_topology_triangle_list;
    info.graphics.vertex_shader = m_vertex_shader;
    info.graphics.pixel_shader = m_fragment_shader;
    info.graphics.dsl = m_dsl;
    info.graphics.samples = 1;
    info.graphics.color_attachment_count = 1;
    info.graphics.color_attachment_formats[0] = ac_image_get_format(image);
    info.graphics.depth_stencil_format = ac_format_d32_sfloat;
    info.name = AC_DEBUG_NAME("pbr");

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipelines.pbr));

    info.name = AC_DEBUG_NAME("pbr_double_sided");
    rasterizer.cull_mode = ac_cull_mode_none;

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipelines.pbr_double_sided));

    info.name = AC_DEBUG_NAME("pbr_alpha_blended");
    rasterizer.cull_mode = ac_cull_mode_none;

    ac_blend_attachment_state* att =
      &info.graphics.blend_state_info.attachment_states[0];

    att->src_factor = ac_blend_factor_src_alpha;
    att->dst_factor = ac_blend_factor_one_minus_src_alpha;
    att->op = ac_blend_op_add;
    att->src_alpha_factor = ac_blend_factor_one_minus_src_alpha;
    att->dst_alpha_factor = ac_blend_factor_zero;
    att->alpha_op = ac_blend_op_add;

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipelines.pbr_alpha_blended));
  }

  return ac_result_success;
}

ac_result
App::stage_prepare(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  memcpy(
    ac_buffer_get_mapped_memory(p->m_camera_buffers[stage->frame]),
    &p->m_camera,
    sizeof(p->m_camera));

  if ((p->m_scene.animations.size() > 0))
  {
    p->m_animation_timer += p->m_dt;
    if (p->m_animation_timer > p->m_scene.animations[p->m_animation_index].end)
    {
      p->m_animation_timer -= p->m_scene.animations[p->m_animation_index].end;
    }
    p->m_scene.update_animation(p->m_animation_index, p->m_animation_timer);
  }

  return ac_result_success;
}

void
App::render_node(ac_rg_stage* stage, Node* node, Material::AlphaMode alpha_mode)
{
  if (node->mesh)
  {
    auto& pipelines = m_pipelines;
    for (Primitive* primitive : node->mesh->primitives)
    {
      if (primitive->material.alpha_mode == alpha_mode)
      {
        ac_pipeline pipeline = NULL;
        switch (alpha_mode)
        {
        case Material::ALPHAMODE_OPAQUE:
        case Material::ALPHAMODE_MASK:
        {
          pipeline = primitive->material.double_sided
                       ? pipelines.pbr_double_sided
                       : pipelines.pbr;
          break;
        }
        case Material::ALPHAMODE_BLEND:
        {
          pipeline = pipelines.pbr_alpha_blended;
          break;
        }
        }

        struct PushData {
          int material;
          int node;
        };

        PushData push_data = {};
        push_data.material = primitive->material.index;
        push_data.node = node->mesh->node;

        ac_cmd_bind_pipeline(stage->cmd, pipeline);
        ac_cmd_bind_set(stage->cmd, m_db, ac_space0, stage->frame);
        ac_cmd_bind_set(stage->cmd, m_db, ac_space1, 0);
        ac_cmd_bind_set(stage->cmd, m_db, ac_space2, 0);

        ac_cmd_push_constants(stage->cmd, sizeof(push_data), &push_data);

        if (primitive->has_indices)
        {
          ac_cmd_draw_indexed(
            stage->cmd,
            primitive->index_count,
            1,
            primitive->first_index,
            0,
            0);
        }
        else
        {
          ac_cmd_draw(stage->cmd, primitive->vertex_count, 1, 0, 0);
        }
      }
    }
  };

  for (auto child : node->children)
  {
    render_node(stage, child, alpha_mode);
  }
}

ac_result
App::stage_cmd(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_cmd   cmd = stage->cmd;
  ac_image image = ac_swapchain_get_image(p->m_swapchain);
  uint32_t width = ac_image_get_width(image);
  uint32_t height = ac_image_get_height(image);

  ac_cmd_set_viewport(cmd, 0, 0, (float)width, (float)height, 0.0f, 1.0f);
  ac_cmd_set_scissor(cmd, 0, 0, width, height);

  Model& model = p->m_scene;

  ac_cmd_bind_pipeline(stage->cmd, p->m_pipelines.pbr);
  ac_cmd_bind_vertex_buffer(stage->cmd, 0, model.vertices, 0);

  if (model.indices)
  {
    ac_cmd_bind_index_buffer(stage->cmd, model.indices, 0, ac_index_type_u32);
  }

  for (auto node : model.nodes)
  {
    p->render_node(stage, node, Material::ALPHAMODE_OPAQUE);
  }

  for (auto node : model.nodes)
  {
    p->render_node(stage, node, Material::ALPHAMODE_MASK);
  }

  for (auto node : model.nodes)
  {
    p->render_node(stage, node, Material::ALPHAMODE_BLEND);
  }

  return ac_result_success;
}

ac_result
App::build_frame(ac_rg_builder builder, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_image      image = ac_swapchain_get_image(p->m_swapchain);
  ac_image_info color = ac_image_get_info(image);
  color.clear_value = {{{0.537, 0.412, 0.471, 1.0}}};
  ac_image_info depth = ac_image_get_info(image);
  depth.format = ac_format_d32_sfloat;
  depth.clear_value = {{{1.0f, 0}}};

  ac_rg_builder_stage_info stage_info {};
  stage_info.name = AC_DEBUG_NAME("main stage");
  stage_info.queue = ac_queue_type_graphics;
  stage_info.commands = ac_queue_type_graphics;
  stage_info.cb_prepare = App::stage_prepare;
  stage_info.cb_cmd = App::stage_cmd;
  stage_info.user_data = p;

  ac_rg_builder_stage stage = ac_rg_builder_create_stage(builder, &stage_info);

  ac_rg_builder_create_resource_info resource_info = {};
  resource_info.image_info = &color;
  resource_info.do_clear = true;

  ac_rg_builder_resource color_image =
    ac_rg_builder_create_resource(builder, &resource_info);

  resource_info.image_info = &depth;
  resource_info.do_clear = true;

  ac_rg_builder_resource depth_image =
    ac_rg_builder_create_resource(builder, &resource_info);

  ac_rg_builder_stage_use_resource_info use_info;

  use_info = {};
  use_info.resource = color_image;
  use_info.token = App::Token::ColorImage;
  use_info.access_attachment = ac_rg_attachment_access_write_bit;
  use_info.usage_bits = ac_image_usage_attachment_bit;

  color_image = ac_rg_builder_stage_use_resource(builder, stage, &use_info);

  use_info = {};
  use_info.resource = depth_image;
  use_info.token = App::Token::DepthImage;
  use_info.access_attachment = ac_rg_attachment_access_write_bit;
  use_info.usage_bits = ac_image_usage_attachment_bit;

  ac_rg_builder_stage_use_resource(builder, stage, &use_info);

  ac_rg_resource_connection connection = {};
  connection.image = ac_swapchain_get_image(p->m_swapchain);
  connection.image_layout = ac_image_layout_present_src;
  connection.wait.fence = p->m_acquire_finished_fences[p->m_frame_index];
  connection.signal.fence = p->m_render_finished_fences[p->m_frame_index];

  ac_rg_builder_export_resource_info export_info = {};
  export_info.resource = color_image;
  export_info.connection = &connection;

  ac_rg_builder_export_resource(builder, &export_info);

  return ac_result_success;
}

ac_result
App::create_stub_images()
{
  ac_image_info info = {};
  info.format = ac_format_r8g8b8a8_unorm;
  info.width = 1;
  info.height = 1;
  info.type = ac_image_type_2d;
  info.layers = 1;
  info.levels = 1;
  info.samples = 1;
  info.usage = ac_image_usage_srv_bit;
  info.name = AC_DEBUG_NAME("stub_image");

  AC_RIF(ac_create_image(m_device, &info, &m_stub_image));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_compute);

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = queue;

  ac_cmd_pool pool;
  AC_RIF(ac_create_cmd_pool(m_device, &pool_info, &pool));

  ac_cmd cmd;
  AC_RIF(ac_create_cmd(pool, &cmd));

  AC_RIF(ac_begin_cmd(cmd));

  ac_image_barrier barrier = {};
  barrier.src_access = ac_access_none;
  barrier.dst_access = ac_access_shader_read_bit;
  barrier.src_stage = ac_pipeline_stage_all_commands_bit;
  barrier.dst_stage = ac_pipeline_stage_all_commands_bit;
  barrier.old_layout = ac_image_layout_undefined;
  barrier.new_layout = ac_image_layout_shader_read;
  barrier.image = m_stub_image;
  ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);

  AC_RIF(ac_end_cmd(cmd));

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  AC_RIF(ac_queue_submit(queue, &submit_info));

  AC_RIF(ac_queue_wait_idle(queue));

  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);

  return ac_result_success;
}

extern "C" ac_result
ac_main(uint32_t argc, char** argv)
{
  App*      app = new App {};
  ac_result res = app->run();
  delete app;
  return res;
}
