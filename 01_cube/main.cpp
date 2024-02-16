#include <string.h>
#include <ac/ac.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "compiled/main.h"

// clang-format off
static const float g_vertex_buffer_data[] = {
 -1.0f,-1.0f,-1.0f,  // -X side
 -1.0f,-1.0f, 1.0f,
 -1.0f, 1.0f, 1.0f,
 -1.0f, 1.0f, 1.0f,
 -1.0f, 1.0f,-1.0f,
 -1.0f,-1.0f,-1.0f,

 -1.0f,-1.0f,-1.0f,  // -Z side
	1.0f, 1.0f,-1.0f,
	1.0f,-1.0f,-1.0f,
 -1.0f,-1.0f,-1.0f,
 -1.0f, 1.0f,-1.0f,
	1.0f, 1.0f,-1.0f,

 -1.0f,-1.0f,-1.0f,  // -Y side
	1.0f,-1.0f,-1.0f,
	1.0f,-1.0f, 1.0f,
 -1.0f,-1.0f,-1.0f,
	1.0f,-1.0f, 1.0f,
 -1.0f,-1.0f, 1.0f,

 -1.0f, 1.0f,-1.0f,  // +Y side
 -1.0f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
 -1.0f, 1.0f,-1.0f,
	1.0f, 1.0f, 1.0f,
	1.0f, 1.0f,-1.0f,

	1.0f, 1.0f,-1.0f,  // +X side
	1.0f, 1.0f, 1.0f,
	1.0f,-1.0f, 1.0f,
	1.0f,-1.0f, 1.0f,
	1.0f,-1.0f,-1.0f,
	1.0f, 1.0f,-1.0f,

 -1.0f, 1.0f, 1.0f,  // +Z side
 -1.0f,-1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
 -1.0f,-1.0f, 1.0f,
	1.0f,-1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
};

static const float g_uv_buffer_data[] = {
	0.0f, 1.0f,  // -X side
	1.0f, 1.0f,
	1.0f, 0.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	0.0f, 1.0f,

	1.0f, 1.0f,  // -Z side
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,

	1.0f, 0.0f,  // -Y side
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	0.0f, 0.0f,

	1.0f, 0.0f,  // +Y side
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,

	1.0f, 0.0f,  // +X side
	0.0f, 0.0f,
	0.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	1.0f, 0.0f,

	0.0f, 0.0f,  // +Z side
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	1.0f, 0.0f,
};

// clang-format on

struct ShaderData {
  glm::mat4 mvp;
  float     position[12 * 3][4];
  float     attr[12 * 3][4];
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
  static constexpr const char* APP_NAME = "01_cube";

  enum Token : uint64_t {
    ColorImage = 0,
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
  ac_pipeline          m_pipeline = {};
  ac_shader            m_vertex_shader = {};
  ac_shader            m_fragment_shader = {};

  ac_buffer m_cbv_buffer[AC_MAX_FRAME_IN_FLIGHT] = {};

  struct {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
  } m_matrices = {};

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
    info.code = main_vs[0];
    info.name = AC_DEBUG_NAME(App::APP_NAME);

    RIF(ac_create_shader(m_device, &info, &m_vertex_shader));
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_pixel;
    info.code = main_fs[0];
    info.name = AC_DEBUG_NAME(App::APP_NAME);

    RIF(ac_create_shader(m_device, &info, &m_fragment_shader));
  }

  {
    ac_shader shaders[] = {
      m_vertex_shader,
      m_fragment_shader,
    };

    ac_dsl_info info = {};
    info.name = AC_DEBUG_NAME(App::APP_NAME);
    info.shader_count = AC_COUNTOF(shaders);
    info.shaders = shaders;

    RIF(ac_create_dsl(m_device, &info, &m_dsl));
  }

  {
    ac_descriptor_buffer_info info = {};
    info.dsl = m_dsl;
    info.max_sets[0] = AC_MAX_FRAME_IN_FLIGHT;
    info.name = AC_DEBUG_NAME(App::APP_NAME);
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
  }

  for (uint32_t f = 0; f < AC_MAX_FRAME_IN_FLIGHT; ++f)
  {
    ac_buffer_info buffer_info = {};
    buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
    buffer_info.usage = ac_buffer_usage_cbv_bit;
    buffer_info.size = sizeof(ShaderData);
    RIF(ac_create_buffer(m_device, &buffer_info, &m_cbv_buffer[f]));

    RIF(ac_buffer_map_memory(m_cbv_buffer[f]));

    ShaderData* mem = (ShaderData*)ac_buffer_get_mapped_memory(m_cbv_buffer[f]);

    for (uint32_t i = 0; i < 12 * 3; i++)
    {
      mem->position[i][0] = g_vertex_buffer_data[i * 3];
      mem->position[i][1] = g_vertex_buffer_data[i * 3 + 1];
      mem->position[i][2] = g_vertex_buffer_data[i * 3 + 2];
      mem->position[i][3] = 1.0f;
      mem->attr[i][0] = g_uv_buffer_data[2 * i];
      mem->attr[i][1] = g_uv_buffer_data[2 * i + 1];
      mem->attr[i][2] = 0;
      mem->attr[i][3] = 0;
    }

    ac_descriptor descriptor = {};
    descriptor.buffer = m_cbv_buffer[f];
    ac_descriptor_write write = {};
    write.count = 1;
    write.type = ac_descriptor_type_cbv_buffer;
    write.descriptors = &descriptor;
    ac_update_set(m_db, ac_space0, f, 1, &write);
  }

  glm::vec3 eye = {0.0, 3.0f, 5.0f};
  glm::vec3 origin = {0.0f, 0.0f, 0.0f};
  glm::vec3 up = {0.0f, 1.0f, 0.0f};

  ac_window_state state = ac_window_get_state();

  m_matrices.projection = glm::perspective(
    glm::radians(45.0f),
    (float)state.width / (float)state.height,
    0.1f,
    100.0f);

  m_matrices.view = glm::lookAt(eye, origin, up);
  m_matrices.model = glm::identity<glm::mat4>();

  RIF(create_window_dependents());

  m_running = true;
}

App::~App()
{
  if (m_device)
  {
    RIF(ac_queue_wait_idle(
      ac_device_get_queue(m_device, ac_queue_type_graphics)));

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_buffer(m_cbv_buffer[i]);
      ac_destroy_fence(m_render_finished_fences[i]);
      ac_destroy_fence(m_acquire_finished_fences[i]);
    }

    ac_destroy_pipeline(m_pipeline);
    ac_destroy_descriptor_buffer(m_db);
    ac_destroy_dsl(m_dsl);
    ac_destroy_shader(m_vertex_shader);
    ac_destroy_shader(m_fragment_shader);

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

  ac_image image = ac_swapchain_get_image(m_swapchain);

  ac_destroy_pipeline(m_pipeline);

  ac_pipeline_info info = {};
  info.type = ac_pipeline_type_graphics;
  info.name = AC_DEBUG_NAME(App::APP_NAME);
  info.graphics.vertex_shader = m_vertex_shader;
  info.graphics.pixel_shader = m_fragment_shader;
  info.graphics.dsl = m_dsl;
  info.graphics.topology = ac_primitive_topology_triangle_list;
  info.graphics.samples = 1;
  info.graphics.color_attachment_count = 1;
  info.graphics.color_attachment_formats[0] = ac_image_get_format(image);
  info.graphics.rasterizer_info.cull_mode = ac_cull_mode_back;
  info.graphics.rasterizer_info.front_face = ac_front_face_counter_clockwise;
  info.graphics.rasterizer_info.polygon_mode = ac_polygon_mode_fill;

  AC_RIF(ac_create_pipeline(m_device, &info, &m_pipeline));

  return ac_result_success;
}

ac_result
App::stage_prepare(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  glm::mat4 mvp, model, vp;

  vp = p->m_matrices.projection * p->m_matrices.view;
  model = p->m_matrices.model;
  p->m_matrices.model = glm::rotate(model, 0.01f, glm::vec3(0.0, 1.0, 0.0));
  mvp = vp * p->m_matrices.model;

  memcpy(
    ac_buffer_get_mapped_memory(p->m_cbv_buffer[stage->frame]),
    &mvp,
    sizeof(mvp));

  return ac_result_success;
}

ac_result
App::stage_cmd(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_cmd cmd = stage->cmd;

  ac_image image = ac_swapchain_get_image(p->m_swapchain);
  uint32_t width = ac_image_get_width(image);
  uint32_t height = ac_image_get_height(image);

  ac_cmd_set_viewport(cmd, 0, 0, (float)width, (float)height, 0.0f, 1.0f);
  ac_cmd_set_scissor(cmd, 0, 0, width, height);

  ac_cmd_bind_pipeline(cmd, p->m_pipeline);
  ac_cmd_bind_set(cmd, p->m_db, ac_space0, stage->frame);
  ac_cmd_draw(cmd, 36, 1, 0, 0);

  return ac_result_success;
}

ac_result
App::build_frame(ac_rg_builder builder, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_image      image = ac_swapchain_get_image(p->m_swapchain);
  ac_image_info result_info = ac_image_get_info(image);
  result_info.clear_value = {{{0.612, 0.702, 0.502, 1.0}}};

  ac_rg_builder_stage_info stage_info {};
  stage_info.name = AC_DEBUG_NAME("main stage");
  stage_info.queue = ac_queue_type_graphics;
  stage_info.commands = ac_queue_type_graphics;
  stage_info.cb_prepare = App::stage_prepare;
  stage_info.cb_cmd = App::stage_cmd;
  stage_info.user_data = p;

  ac_rg_builder_stage stage = ac_rg_builder_create_stage(builder, &stage_info);

  ac_rg_builder_create_resource_info resource_info = {};
  resource_info.image_info = &result_info;
  resource_info.do_clear = true;

  ac_rg_builder_resource resource =
    ac_rg_builder_create_resource(builder, &resource_info);

  ac_rg_builder_stage_use_resource_info use_info = {};
  use_info.resource = resource;
  use_info.token = App::Token::ColorImage;
  use_info.access_attachment = ac_rg_attachment_access_write_bit;
  use_info.usage_bits = ac_image_usage_attachment_bit;

  resource = ac_rg_builder_stage_use_resource(builder, stage, &use_info);

  ac_rg_resource_connection connection = {};
  connection.image = ac_swapchain_get_image(p->m_swapchain);
  connection.image_layout = ac_image_layout_present_src;
  connection.wait.fence = p->m_acquire_finished_fences[p->m_frame_index];
  connection.signal.fence = p->m_render_finished_fences[p->m_frame_index];

  ac_rg_builder_export_resource_info export_info = {};
  export_info.resource = resource;
  export_info.connection = &connection;

  ac_rg_builder_export_resource(builder, &export_info);
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