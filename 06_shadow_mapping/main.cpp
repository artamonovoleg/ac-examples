#include <string.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <ac/ac.h>

#include "compiled/shadow_mapping.h"
#include "compiled/shadow_mapping_depth.h"

#define SHADOW_MAP_SIZE 2048

struct UBO {
  glm::mat4 projection;
  glm::mat4 view;
  glm::mat4 light_space_matrix;
  glm::mat4 transforms[4];
  glm::vec4 light_pos;
  glm::vec4 view_pos;
};

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec3 color;
};

static constexpr glm::vec3 GRAY = {0.3, 0.3, 0.3};
static constexpr glm::vec3 PINK = {0.922, 0.624, 0.937};

static constexpr Vertex PLANE_VERTICES[] = {
  {{25.0f, -0.5f, -25.0f}, {0.0f, 1.0f, 0.0f}, GRAY},  //, {25.0f, 25.0f}},
  {{-25.0f, -0.5f, -25.0f}, {0.0f, 1.0f, 0.0f}, GRAY}, //{0.0f, 25.0f}},
  {{25.0f, -0.5f, 25.0f}, {0.0f, 1.0f, 0.0f}, GRAY},   // {25.0f, 0.0f}},
  {{-25.0f, -0.5f, -25.0f}, {0.0f, 1.0f, 0.0f}, GRAY}, // {0.0f, 25.0f}},
  {{-25.0f, -0.5f, 25.0f}, {0.0f, 1.0f, 0.0f}, GRAY},  // {0.0f, 0.0f}},
  {{25.0f, -0.5f, 25.0f}, {0.0f, 1.0f, 0.0f}, GRAY},   //{25.0f, 0.0f}},
};

static constexpr Vertex CUBE_VERTICES[] = {
  {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK}, // {0.0f, 0.0f}},
  {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK},   // {1.0f, 1.0f}},
  {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK},  // {1.0f, 0.0f}},
  {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK},   // {1.0f, 1.0f}},
  {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK}, // {0.0f, 0.0f}},
  {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, PINK},  // {0.0f, 1.0f}},

  {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK}, // {0.0f, 0.0f}},
  {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK},  // {1.0f, 0.0f}},
  {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK},   // {1.0f, 1.0f}},
  {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK},   // {1.0f, 1.0f}},
  {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK},  //{0.0f, 1.0f}},
  {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, PINK}, // {0.0f, 0.0f}},

  {{-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{-1.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, PINK},  // {1.0f, 1.0f}},
  {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{-1.0f, -1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, PINK},  // {0.0f, 0.0f}},
  {{-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},

  {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, PINK},   //{1.0f, 0.0f}},
  {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, PINK},  // {1.0f, 1.0f}},
  {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, PINK},  // {0.0f, 0.0f}},

  {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, PINK},  // {1.0f, 1.0f}},
  {{1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{-1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, PINK},  // {0.0f, 0.0f}},
  {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},

  {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, PINK},  // {1.0f, 1.0f}},
  {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, PINK},   // {1.0f, 0.0f}},
  {{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, PINK}, // {0.0f, 1.0f}},
  {{-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, PINK},  // {0.0f, 0.0f}},
};

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
  static constexpr const char* APP_NAME = "06_shadow_mapping";

  enum Token : uint64_t {
    ShadowImage = 0,
    ColorImage = 1,
    DepthImage = 2,
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

  ac_dsl               m_shadow_mapping_dsl = {};
  ac_dsl               m_shadow_mapping_depth_dsl = {};
  ac_descriptor_buffer m_db = {};
  ac_descriptor_buffer m_db_depth = {};

  struct {
    ac_pipeline shadow_mapping;
    ac_pipeline shadow_mapping_depth;
  } m_pipelines = {};

  ac_shader m_vs_shadow_mapping_shader;
  ac_shader m_fs_shadow_mapping_shader;
  ac_shader m_vs_shadow_mapping_depth_shader;

  ac_buffer  m_cube_vb = {};
  ac_buffer  m_plane_vb = {};
  ac_buffer  m_ubo_buffers[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_sampler m_sampler = {};

  UBO m_ubo = {};

  static void
  window_callback(const ac_window_event* event, void* ud);

  static ac_result
  stage_prepare(ac_rg_stage* stage, void* ud);

  static ac_result
  shadow_mapping_depth_stage_cmd(ac_rg_stage* stage, void* ud);
  static ac_result
  shadow_mapping_stage_cmd(ac_rg_stage* stage, void* ud);

  static ac_result
  build_frame(ac_rg_builder builder, void* ud);

  ac_result
  create_window_dependents();

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

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_vertex;
    info.code = shadow_mapping_vs[0];
    info.name = AC_DEBUG_NAME("shadow mapping vs");

    RIF(ac_create_shader(m_device, &info, &m_vs_shadow_mapping_shader));
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_pixel;
    info.code = shadow_mapping_fs[0];
    info.name = AC_DEBUG_NAME("shadow mapping fs");

    RIF(ac_create_shader(m_device, &info, &m_fs_shadow_mapping_shader));
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_vertex;
    info.code = shadow_mapping_depth_vs[0];
    info.name = AC_DEBUG_NAME("shadow mapping depth");

    RIF(ac_create_shader(m_device, &info, &m_vs_shadow_mapping_depth_shader));
  }

  {
    ac_shader shaders[] = {
      m_vs_shadow_mapping_shader,
      m_fs_shadow_mapping_shader,
    };

    ac_dsl_info dsl_info = {};
    dsl_info.shader_count = AC_COUNTOF(shaders);
    dsl_info.shaders = shaders;
    dsl_info.name = AC_DEBUG_NAME("shadow mapping");

    RIF(ac_create_dsl(m_device, &dsl_info, &m_shadow_mapping_dsl));
  }

  {
    ac_dsl_info dsl_info = {};
    dsl_info.shader_count = 1;
    dsl_info.shaders = &m_vs_shadow_mapping_depth_shader;
    dsl_info.name = AC_DEBUG_NAME("shadow mapping depth");

    RIF(ac_create_dsl(m_device, &dsl_info, &m_shadow_mapping_depth_dsl));
  }

  {
    ac_descriptor_buffer_info info = {};
    info.dsl = m_shadow_mapping_dsl;
    info.max_sets[ac_space0] = AC_MAX_FRAME_IN_FLIGHT;
    info.max_sets[ac_space1] = AC_MAX_FRAME_IN_FLIGHT;
    info.name = AC_DEBUG_NAME("shadow mapping");
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
  }

  {
    ac_descriptor_buffer_info info = {};
    info.dsl = m_shadow_mapping_depth_dsl;
    info.max_sets[ac_space0] = AC_MAX_FRAME_IN_FLIGHT;
    info.name = AC_DEBUG_NAME("shadow mapping depth");
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db_depth));
  }

  for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
  {
    ac_buffer_info info = {};
    info.size = sizeof(UBO);
    info.usage = ac_buffer_usage_cbv_bit;
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.name = AC_DEBUG_NAME("ubo");

    RIF(ac_create_buffer(m_device, &info, &m_ubo_buffers[i]));
    RIF(ac_buffer_map_memory(m_ubo_buffers[i]));

    ac_descriptor descriptor = {};
    descriptor.buffer = m_ubo_buffers[i];

    ac_descriptor_write write = {};
    write.type = ac_descriptor_type_cbv_buffer;
    write.count = 1;
    write.descriptors = &descriptor;

    ac_update_set(m_db, ac_space0, i, 1, &write);
    ac_update_set(m_db_depth, ac_space0, i, 1, &write);
  }

  {
    ac_buffer_info info = {};
    info.size = sizeof(CUBE_VERTICES);
    info.usage = ac_buffer_usage_vertex_bit;
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.name = AC_DEBUG_NAME("cube vb");

    RIF(ac_create_buffer(m_device, &info, &m_cube_vb));
    RIF(ac_buffer_map_memory(m_cube_vb));

    memcpy(ac_buffer_get_mapped_memory(m_cube_vb), CUBE_VERTICES, info.size);

    ac_buffer_unmap_memory(m_cube_vb);
  }

  {
    ac_buffer_info info = {};
    info.size = sizeof(PLANE_VERTICES);
    info.usage = ac_buffer_usage_vertex_bit;
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.name = AC_DEBUG_NAME("plane vb");

    RIF(ac_create_buffer(m_device, &info, &m_plane_vb));
    RIF(ac_buffer_map_memory(m_plane_vb));

    memcpy(ac_buffer_get_mapped_memory(m_plane_vb), PLANE_VERTICES, info.size);

    ac_buffer_unmap_memory(m_plane_vb);
  }

  {
    ac_sampler_info info = {};
    info.mag_filter = ac_filter_linear;
    info.min_filter = ac_filter_linear;
    info.address_mode_u = ac_sampler_address_mode_repeat;
    info.address_mode_v = ac_sampler_address_mode_repeat;
    info.address_mode_w = ac_sampler_address_mode_repeat;
    info.anisotropy_enable = true;
    info.max_anisotropy = 16;
    info.mipmap_mode = ac_sampler_mipmap_mode_linear;
    RIF(ac_create_sampler(m_device, &info, &m_sampler));
  }

  glm::vec3 eye = {2.0f, 7.0f, -14.0f};
  glm::vec3 origin = {0.0f, -1.0f, 0.0f};
  glm::vec3 up = {0.0f, 1.0f, 0.0f};

  ac_window_state state = ac_window_get_state();

  m_ubo.projection = glm::perspective(
    glm::radians(45.0f),
    (float)state.width / (float)state.height,
    0.1f,
    100.0f);

  m_ubo.view = glm::lookAt(eye, origin, up);

  // m_ubo.model = glm::identity<glm::mat4>();

  auto& m = m_ubo.transforms;

  m[0] = glm::identity<glm::mat4>();

  m[1] = glm::identity<glm::mat4>();
  m[1] = glm::translate(m[0], glm::vec3(0.0f, 3.5f, -1.0));

  m[2] = glm::identity<glm::mat4>();
  m[2] = glm::translate(m[2], glm::vec3(3.0f, 1.5f, 2.0));

  m[3] = glm::identity<glm::mat4>();
  m[3] = glm::translate(m[3], glm::vec3(-2.0f, 2.0f, 3.0));
  m[3] = glm::rotate(
    m[3],
    glm::radians(60.0f),
    glm::normalize(glm::vec3(1.0, 0.0, 1.0)));

  glm::vec3 light_pos(0.5f, 2.0f, 2.0f);

  light_pos.x = 40.0f;
  light_pos.y = -40.0f;
  light_pos.z = 30.0f;

  this->m_ubo.light_pos = glm::vec4(light_pos, 0.0);

  glm::mat4 depth_projection_matrix =
    glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 96.0f);
  glm::mat4 depth_view_matrix =
    glm::lookAt(light_pos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
  glm::mat4 depth_model_matrix = glm::mat4(1.0f);

  this->m_ubo.light_space_matrix =
    depth_projection_matrix * depth_view_matrix * depth_model_matrix;

  RIF(create_window_dependents());

  m_running = true;
}

App::~App()
{
  if (m_device)
  {
    RIF(ac_queue_wait_idle(
      ac_device_get_queue(m_device, ac_queue_type_graphics)));

    ac_destroy_sampler(m_sampler);

    ac_destroy_buffer(m_cube_vb);
    ac_destroy_buffer(m_plane_vb);

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_buffer(m_ubo_buffers[i]);
    }

    ac_destroy_descriptor_buffer(m_db_depth);
    ac_destroy_descriptor_buffer(m_db);
    ac_destroy_dsl(m_shadow_mapping_depth_dsl);
    ac_destroy_dsl(m_shadow_mapping_dsl);
    ac_destroy_pipeline(m_pipelines.shadow_mapping);
    ac_destroy_pipeline(m_pipelines.shadow_mapping_depth);
    ac_destroy_shader(m_vs_shadow_mapping_depth_shader);
    ac_destroy_shader(m_vs_shadow_mapping_shader);
    ac_destroy_shader(m_fs_shadow_mapping_shader);

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

  while (m_running)
  {
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
    ac_destroy_pipeline(m_pipelines.shadow_mapping);
    ac_destroy_pipeline(m_pipelines.shadow_mapping_depth);

    ac_image image = ac_swapchain_get_image(m_swapchain);

    ac_pipeline_info info = {};
    info.type = ac_pipeline_type_graphics;
    info.name = AC_DEBUG_NAME("shadow mapping");
    info.graphics.dsl = m_shadow_mapping_dsl;
    info.graphics.vertex_shader = m_vs_shadow_mapping_shader;
    info.graphics.pixel_shader = m_fs_shadow_mapping_shader;
    info.graphics.topology = ac_primitive_topology_triangle_list;
    info.graphics.samples = 1;
    info.graphics.color_attachment_count = 1;
    info.graphics.color_attachment_formats[0] = ac_image_get_format(image);
    info.graphics.depth_stencil_format = ac_format_d32_sfloat;

    ac_depth_state_info* depth = &info.graphics.depth_state_info;
    depth->compare_op = ac_compare_op_less;
    depth->depth_test = true;
    depth->depth_write = true;

    ac_rasterizer_state_info* rasterizer = &info.graphics.rasterizer_info;
    rasterizer->front_face = ac_front_face_counter_clockwise;
    rasterizer->cull_mode = ac_cull_mode_back;
    rasterizer->polygon_mode = ac_polygon_mode_fill;

    ac_vertex_layout* layout = &info.graphics.vertex_layout;
    layout->binding_count = 1;
    layout->bindings[0].stride = sizeof(Vertex);
    layout->bindings[0].input_rate = ac_input_rate_vertex;
    layout->attribute_count = 3;
    layout->attributes[0].format = ac_format_r32g32b32_sfloat;
    layout->attributes[0].offset = AC_OFFSETOF(Vertex, position);
    layout->attributes[0].semantic = ac_attribute_semantic_position;
    layout->attributes[1].format = ac_format_r32g32b32_sfloat;
    layout->attributes[1].offset = AC_OFFSETOF(Vertex, normal);
    layout->attributes[1].semantic = ac_attribute_semantic_normal;
    layout->attributes[2].format = ac_format_r32g32b32_sfloat;
    layout->attributes[2].offset = AC_OFFSETOF(Vertex, color);
    layout->attributes[2].semantic = ac_attribute_semantic_color;

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipelines.shadow_mapping));

    layout->binding_count = 1;
    layout->bindings[0].input_rate = ac_input_rate_vertex;
    layout->bindings[0].stride = sizeof(Vertex);
    layout->attribute_count = 1;
    layout->attributes[0].format = ac_format_r32g32b32_sfloat;
    layout->attributes[0].offset = AC_OFFSETOF(Vertex, position);
    layout->attributes[0].semantic = ac_attribute_semantic_position;

    rasterizer->cull_mode = ac_cull_mode_none;
    rasterizer->depth_bias_enable = true;
    rasterizer->depth_bias_slope_factor = 1.75f;
    rasterizer->depth_bias_constant_factor = 1.25f;

    depth->compare_op = ac_compare_op_less_or_equal;

    info.name = AC_DEBUG_NAME("shadow mapping depth");
    info.graphics.dsl = m_shadow_mapping_depth_dsl;
    info.graphics.vertex_shader = m_vs_shadow_mapping_depth_shader;
    info.graphics.pixel_shader = NULL;
    info.graphics.color_attachment_count = 0;

    AC_RIF(
      ac_create_pipeline(m_device, &info, &m_pipelines.shadow_mapping_depth));
  }

  return ac_result_success;
}

ac_result
App::stage_prepare(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  UBO* ubo = (UBO*)ac_buffer_get_mapped_memory(p->m_ubo_buffers[stage->frame]);

  // glm::mat4 model;
  // model = p->m_ubo.model;
  // p->m_ubo.model = glm::rotate(model, 0.01f, glm::vec3(0.0, 1.0, 0.0));

  memcpy(ubo, &p->m_ubo, sizeof(UBO));

  return ac_result_success;
}

ac_result
App::shadow_mapping_depth_stage_cmd(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_cmd cmd = stage->cmd;

  ac_cmd_set_viewport(
    cmd,
    0,
    0,
    (float)SHADOW_MAP_SIZE,
    (float)SHADOW_MAP_SIZE,
    0.0f,
    1.0f);
  ac_cmd_set_scissor(cmd, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

  ac_cmd_bind_pipeline(cmd, p->m_pipelines.shadow_mapping_depth);
  ac_cmd_bind_set(cmd, p->m_db_depth, ac_space0, stage->frame);

  ac_cmd_bind_vertex_buffer(cmd, 0, p->m_cube_vb, 0);

  for (uint32_t i = 1; i < 4; ++i)
  {
    ac_cmd_push_constants(cmd, sizeof(i), &i);
    ac_cmd_draw(cmd, AC_COUNTOF(CUBE_VERTICES), 1, 0, 0);
  }

  return ac_result_success;
}

ac_result
App::shadow_mapping_stage_cmd(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_cmd   cmd = stage->cmd;
  ac_image image = ac_swapchain_get_image(p->m_swapchain);
  uint32_t width = ac_image_get_width(image);
  uint32_t height = ac_image_get_height(image);

  ac_descriptor sampler_descriptor = {};
  sampler_descriptor.sampler = p->m_sampler;

  ac_descriptor image_descriptor = {};
  image_descriptor.image = ac_rg_stage_get_image(stage, 0);

  ac_descriptor_write writes[2] = {};
  writes[0].count = 1;
  writes[0].type = ac_descriptor_type_sampler;
  writes[0].descriptors = &sampler_descriptor;
  writes[1].count = 1;
  writes[1].type = ac_descriptor_type_srv_image;
  writes[1].descriptors = &image_descriptor;

  ac_update_set(p->m_db, ac_space1, stage->frame, AC_COUNTOF(writes), writes);

  ac_cmd_set_viewport(cmd, 0, 0, (float)width, (float)height, 0.0f, 1.0f);
  ac_cmd_set_scissor(cmd, 0, 0, width, height);

  ac_cmd_bind_pipeline(cmd, p->m_pipelines.shadow_mapping);
  ac_cmd_bind_set(cmd, p->m_db, ac_space0, stage->frame);
  ac_cmd_bind_set(cmd, p->m_db, ac_space1, stage->frame);

  ac_cmd_bind_vertex_buffer(cmd, 0, p->m_plane_vb, 0);

  uint32_t i = 0;
  ac_cmd_push_constants(cmd, sizeof(i), &i);
  ac_cmd_draw(cmd, AC_COUNTOF(PLANE_VERTICES), 1, 0, 0);

  ac_cmd_bind_vertex_buffer(cmd, 0, p->m_cube_vb, 0);

  for (i = 1; i < 4; ++i)
  {
    ac_cmd_push_constants(cmd, sizeof(i), &i);
    ac_cmd_draw(cmd, AC_COUNTOF(CUBE_VERTICES), 1, 0, 0);
  }

  return ac_result_success;
}

ac_result
App::build_frame(ac_rg_builder builder, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_rg_builder_resource shadow_map;
  {
    ac_image      image = ac_swapchain_get_image(p->m_swapchain);
    ac_image_info depth = ac_image_get_info(image);
    depth.width = SHADOW_MAP_SIZE;
    depth.height = SHADOW_MAP_SIZE;
    depth.format = ac_format_d32_sfloat;
    depth.clear_value = {{{1.0f, 0}}};

    ac_rg_builder_stage_info stage_info = {};
    stage_info.name = AC_DEBUG_NAME("shadow mapping depth");
    stage_info.queue = ac_queue_type_graphics;
    stage_info.commands = ac_queue_type_graphics;
    stage_info.cb_prepare = App::stage_prepare;
    stage_info.cb_cmd = App::shadow_mapping_depth_stage_cmd;
    stage_info.user_data = p;

    ac_rg_builder_stage stage =
      ac_rg_builder_create_stage(builder, &stage_info);

    ac_rg_builder_create_resource_info resource_info = {};
    resource_info.image_info = &depth;
    resource_info.do_clear = true;

    shadow_map = ac_rg_builder_create_resource(builder, &resource_info);

    ac_rg_builder_stage_use_resource_info use_info = {};
    use_info.resource = shadow_map;
    use_info.token = App::Token::ShadowImage;
    use_info.access_attachment = ac_rg_attachment_access_write_bit;
    use_info.usage_bits = ac_image_usage_attachment_bit;

    shadow_map = ac_rg_builder_stage_use_resource(builder, stage, &use_info);
  }

  {
    ac_image      image = ac_swapchain_get_image(p->m_swapchain);
    ac_image_info color = ac_image_get_info(image);
    color.clear_value = {{{0.580, 0.659, 0.604, 1.0}}};
    ac_image_info depth = ac_image_get_info(image);
    depth.format = ac_format_d32_sfloat;
    depth.clear_value = {{{1.0f, 0}}};

    ac_rg_builder_stage_info stage_info = {};
    stage_info.name = AC_DEBUG_NAME("shadow mapping");
    stage_info.queue = ac_queue_type_graphics;
    stage_info.commands = ac_queue_type_graphics;
    stage_info.cb_cmd = App::shadow_mapping_stage_cmd;
    stage_info.user_data = p;

    ac_rg_builder_stage stage =
      ac_rg_builder_create_stage(builder, &stage_info);

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

    depth_image = ac_rg_builder_stage_use_resource(builder, stage, &use_info);
    AC_UNUSED(depth_image);

    use_info.resource = shadow_map;
    use_info.usage_bits = ac_image_usage_srv_bit;
    use_info.access_read.stages = ac_pipeline_stage_pixel_shader_bit;
    use_info.access_read.access = ac_access_shader_read_bit;
    use_info.token = App::Token::ShadowImage;
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
  }

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
