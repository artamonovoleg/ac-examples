#include <string.h>
#include <ac/ac.h>
#include <glm/glm.hpp>
#include "compiled/main.h"

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

static constexpr glm::vec3 VERTICES[] = {
  {0.5f, 0.5f, 0.0f},
  {-0.5f, 0.5f, 0.0f},
  {0.0f, -0.5f, 0.0f},
};

static constexpr uint16_t INDICES[] = {0, 1, 2};

class App {
private:
  static constexpr const char* APP_NAME = "08_rayquery";

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
  ac_pipeline          m_pipeline = {};
  ac_descriptor_buffer m_db = {};
  ac_shader            m_vertex_shader = {};
  ac_shader            m_fragment_shader = {};

  ac_buffer m_vertex_buffer = {};
  ac_buffer m_index_buffer = {};

  ac_as m_blas = {};
  ac_as m_tlas = {};

  ac_cmd_pool m_cmd_pool = {};
  ac_cmd      m_cmd = {};

  bool m_rayquery = {};

  static void
  window_callback(const ac_window_event* event, void* ud);

  static ac_result
  build_frame(ac_rg_builder builder, void* ud);
  static ac_result
  stage_cmd(ac_rg_stage* stage, void* ud);

  ac_result
  create_window_dependents();

  ac_result
  create_blas();
  ac_result
  create_tlas();
  ac_result
  create_resources();

public:
  App();
  ~App();

  ac_result
  run();
};

ac_result
App::create_blas()
{
  ac_as_geometry as_geometry = {};
  as_geometry.bits = ac_geometry_opaque_bit;
  as_geometry.type = ac_geometry_type_triangles;
  as_geometry.triangles.vertex_format = ac_format_r32g32b32_sfloat;
  as_geometry.triangles.vertex_count = AC_COUNTOF(VERTICES);
  as_geometry.triangles.index_count = AC_COUNTOF(INDICES);
  as_geometry.triangles.vertex_stride = sizeof(VERTICES[0]);
  as_geometry.triangles.vertex.buffer = m_vertex_buffer;
  as_geometry.triangles.index_type = ac_index_type_u16;
  as_geometry.triangles.index.buffer = m_index_buffer;

  ac_as_info info = {};
  info.type = ac_as_type_bottom_level;
  info.count = 1;
  info.geometries = &as_geometry;
  info.name = AC_DEBUG_NAME("blas");

  AC_RIF(ac_create_as(m_device, &info, &m_blas));

  ac_buffer scratch_buffer;

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_gpu_only;
    info.usage = ac_buffer_usage_srv_bit | ac_buffer_usage_raytracing_bit;
    info.size = ac_as_get_scratch_size(m_blas);

    AC_RIF(ac_create_buffer(m_device, &info, &scratch_buffer));
  }

  AC_RIF(ac_reset_cmd_pool(m_cmd_pool));
  AC_RIF(ac_begin_cmd(m_cmd));

  ac_as_build_info build_info = {};
  build_info.as = m_blas;
  build_info.scratch_buffer = scratch_buffer;

  ac_cmd_build_as(m_cmd, &build_info);

  AC_RIF(ac_end_cmd(m_cmd));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &m_cmd;
  ac_queue_submit(queue, &submit_info);
  ac_queue_wait_idle(queue);

  ac_destroy_buffer(scratch_buffer);

  return ac_result_success;
}

ac_result
App::create_tlas()
{
  ac_device_properties props = ac_device_get_properties(m_device);

  // clang-format off
	ac_transform_matrix transform = {
	  1.0f, 0.0f, 0.0f, 0.0f,
	  0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
	};
  // clang-format on

  ac_as_instance instance = {};
  instance.transform = transform;
  instance.instance_index = 0;
  instance.mask = 0xff;
  instance.instance_sbt_offset = 0;
  instance.bits = ac_as_instance_triangle_facing_cull_disable_bit;
  instance.as = m_blas;

  ac_buffer instances_buffer;

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_raytracing_bit;
    info.size = props.as_instance_size;
    AC_RIF(ac_create_buffer(m_device, &info, &instances_buffer));
  }

  AC_RIF(ac_buffer_map_memory(instances_buffer));
  ac_write_as_instances(
    m_device,
    1,
    &instance,
    ac_buffer_get_mapped_memory(instances_buffer));
  ac_buffer_unmap_memory(instances_buffer);

  ac_as_info info = {};
  info.type = ac_as_type_top_level;
  info.count = 1;
  info.instances = &instance;
  info.instances_buffer = instances_buffer;
  info.instances_buffer_offset = 0;
  info.name = AC_DEBUG_NAME("tlas");

  AC_RIF(ac_create_as(m_device, &info, &m_tlas));

  ac_buffer scratch_buffer;

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_gpu_only;
    info.usage = ac_buffer_usage_srv_bit | ac_buffer_usage_raytracing_bit;
    info.size = ac_as_get_scratch_size(m_tlas);

    AC_RIF(ac_create_buffer(m_device, &info, &scratch_buffer));
  }

  AC_RIF(ac_reset_cmd_pool(m_cmd_pool));
  AC_RIF(ac_begin_cmd(m_cmd));

  ac_as_build_info build_info = {};
  build_info.scratch_buffer = scratch_buffer;
  build_info.as = m_tlas;

  ac_cmd_build_as(m_cmd, &build_info);

  AC_RIF(ac_end_cmd(m_cmd));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &m_cmd;
  ac_queue_submit(queue, &submit_info);
  ac_queue_wait_idle(queue);

  ac_destroy_buffer(scratch_buffer);
  ac_destroy_buffer(instances_buffer);

  return ac_result_success;
}

ac_result
App::create_resources()
{
  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_vertex_bit | ac_buffer_usage_raytracing_bit;
    info.size = sizeof(VERTICES);

    AC_RIF(ac_create_buffer(m_device, &info, &m_vertex_buffer));
    AC_RIF(ac_buffer_map_memory(m_vertex_buffer));
    memcpy(ac_buffer_get_mapped_memory(m_vertex_buffer), VERTICES, info.size);
    ac_buffer_unmap_memory(m_vertex_buffer);
  }

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_index_bit | ac_buffer_usage_raytracing_bit;
    info.size = sizeof(INDICES);

    AC_RIF(ac_create_buffer(m_device, &info, &m_index_buffer));
    AC_RIF(ac_buffer_map_memory(m_index_buffer));
    memcpy(ac_buffer_get_mapped_memory(m_index_buffer), INDICES, info.size);
    ac_buffer_unmap_memory(m_index_buffer);
  }

  return ac_result_success;
}

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

  m_rayquery = ac_device_support_raytracing(m_device);

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

  if (m_rayquery)
  {
    {
      ac_cmd_pool_info info = {};
      info.queue = ac_device_get_queue(m_device, ac_queue_type_graphics);
      RIF(ac_create_cmd_pool(m_device, &info, &m_cmd_pool));
    }

    RIF(ac_create_cmd(m_cmd_pool, &m_cmd));

    RIF(create_resources());
    RIF(create_blas());
    RIF(create_tlas());
  }

  uint32_t permutation = 0;

  if (m_rayquery)
  {
    permutation = 1;
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_vertex;
    info.code = main_vs[permutation];
    info.name = AC_DEBUG_NAME(App::APP_NAME);

    RIF(ac_create_shader(m_device, &info, &m_vertex_shader));
  }

  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_pixel;
    info.code = main_fs[permutation];
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

  if (m_rayquery)
  {
    {
      ac_descriptor_buffer_info info = {};
      info.dsl = m_dsl;
      info.max_sets[ac_space0] = 1;
      RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
    }

    {
      ac_descriptor tlas = {};
      tlas.as = m_tlas;

      ac_descriptor_write writes[1] = {};

      writes[0].descriptors = &tlas;
      writes[0].type = ac_descriptor_type_as;
      writes[0].count = 1;

      ac_update_set(m_db, ac_space0, 0, AC_COUNTOF(writes), writes);
    }
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

    ac_destroy_descriptor_buffer(m_db);

    ac_destroy_as(m_blas);
    ac_destroy_as(m_tlas);

    ac_destroy_buffer(m_vertex_buffer);
    ac_destroy_buffer(m_index_buffer);

    ac_destroy_cmd(m_cmd);
    ac_destroy_cmd_pool(m_cmd_pool);

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_fence(m_render_finished_fences[i]);
      ac_destroy_fence(m_acquire_finished_fences[i]);
    }

    ac_destroy_pipeline(m_pipeline);
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

  {
    ac_destroy_pipeline(m_pipeline);

    ac_image image = ac_swapchain_get_image(m_swapchain);

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

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipeline));
  }

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
  if (p->m_rayquery)
  {
    struct PushData {
      float width;
      float height;
    };
    PushData data = {(float)width, (float)height};
    ac_cmd_push_constants(cmd, sizeof(PushData), &data);
    ac_cmd_bind_set(cmd, p->m_db, ac_space0, 0);
  }
  ac_cmd_draw(cmd, 3, 1, 0, 0);

  return ac_result_success;
}

ac_result
App::build_frame(ac_rg_builder builder, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_image      image = ac_swapchain_get_image(p->m_swapchain);
  ac_image_info result_info = ac_image_get_info(image);
  result_info.clear_value = {{{0.831f, 0.878f, 0.608f, 1.0f}}};

  ac_rg_builder_stage_info stage_info {};
  stage_info.name = AC_DEBUG_NAME("main stage");
  stage_info.queue = ac_queue_type_graphics;
  stage_info.commands = ac_queue_type_graphics;
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
