#include <ac/ac.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

struct Camera {
  glm::mat4 view_inverse;
  glm::mat4 proj_inverse;
};

class App {
private:
  static constexpr const char* APP_NAME = "08_raytracing";

  bool m_running = {};
  bool m_window_changed = {};

  ac_wsi m_wsi = {};

  ac_device    m_device = {};
  ac_swapchain m_swapchain = {};
  ac_fence     m_acquire_finished_fences[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_fence     m_render_finished_fences[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_cmd_pool  m_cmd_pools[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_cmd       m_cmds[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_fence     m_fence = {};
  uint64_t     m_fence_value = {};
  uint32_t     m_frame_index = {};

  ac_dsl               m_dsl = {};
  ac_pipeline          m_pipeline = {};
  ac_descriptor_buffer m_db = {};
  ac_shader            m_raygen = {};
  ac_shader            m_closest_hit = {};
  ac_shader            m_miss = {};

  ac_image  m_output_image = {};
  ac_buffer m_vertex_buffer = {};
  ac_buffer m_index_buffer = {};

  ac_sbt m_sbt = {};
  ac_as  m_blas = {};
  ac_as  m_tlas = {};

  static void
  window_callback(const ac_window_event* event, void* ud);

  ac_result
  create_window_dependents();

  ac_result
  frame();

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

  AC_RIF(ac_reset_cmd_pool(m_device, m_cmd_pools[m_frame_index]));
  AC_RIF(ac_begin_cmd(m_cmds[0]));

  ac_as_build_info build_info = {};
  build_info.as = m_blas;
  build_info.scratch_buffer = scratch_buffer;

  ac_cmd_build_as(m_cmds[0], &build_info);

  AC_RIF(ac_end_cmd(m_cmds[0]));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &m_cmds[0];
  ac_queue_submit(queue, &submit_info);
  ac_queue_wait_idle(queue);

  ac_destroy_buffer(scratch_buffer);

  return ac_result_success;
}

ac_result
App::create_tlas()
{
  ac_device_properties props = ac_device_get_properties(m_device);

  ac_transform_matrix transform = {
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
  };

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

  ac_as_info info = {};
  info.type = ac_as_type_top_level;
  info.count = 1;
  info.instances = &instance;
  info.instances_buffer = instances_buffer;
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

  AC_RIF(ac_reset_cmd_pool(m_device, m_cmd_pools[m_frame_index]));
  AC_RIF(ac_begin_cmd(m_cmds[0]));

  ac_as_build_info build_info = {};
  build_info.scratch_buffer = scratch_buffer;
  build_info.as = m_tlas;

  ac_cmd_build_as(m_cmds[0], &build_info);

  AC_RIF(ac_end_cmd(m_cmds[0]));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &m_cmds[0];
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
    ac_image image = ac_swapchain_get_image(m_swapchain);

    ac_image_info info = ac_image_get_info(image);

    info.format = ac_format_to_unorm(info.format);
    info.usage = ac_image_usage_transfer_src_bit | ac_image_usage_uav_bit;

    AC_RIF(ac_create_image(m_device, &info, &m_output_image));
  }

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
    RIF(ac_create_device(&info, &m_device));
  }

  {
    ac_fence_info info = {};
    RIF(ac_create_fence(m_device, &info, &m_fence));
  }

  for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
  {
    {
      ac_fence_info info = {};
      info.bits = ac_fence_present_bit;
      RIF(ac_create_fence(m_device, &info, &m_acquire_finished_fences[i]));
      RIF(ac_create_fence(m_device, &info, &m_render_finished_fences[i]));
    }
    {
      ac_cmd_pool_info info = {};
      info.queue = ac_device_get_queue(m_device, ac_queue_type_graphics);
      RIF(ac_create_cmd_pool(m_device, &info, &m_cmd_pools[i]));
      RIF(ac_create_cmd(m_cmd_pools[i], &m_cmds[i]));
    }
  }

  RIF(create_resources());
  RIF(create_blas());
  RIF(create_tlas());
  {
    ac_shader_info info = {};
    info.stage = ac_shader_stage_raygen;
    info.code = main_raygen[0];
    RIF(ac_create_shader(m_device, &info, &m_raygen));
    info.stage = ac_shader_stage_closest_hit;
    info.code = main_closest_hit[0];
    RIF(ac_create_shader(m_device, &info, &m_closest_hit));
    info.stage = ac_shader_stage_miss;
    info.code = main_miss[0];
    RIF(ac_create_shader(m_device, &info, &m_miss));
  }

  {
    ac_shader shaders[] = {
      m_raygen,
      m_miss,
      m_closest_hit,
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
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
  }

  for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
  {
    ac_descriptor image = {};
    image.image = m_output_image;

    ac_descriptor tlas = {};
    tlas.as = m_tlas;

    ac_descriptor_write writes[2] = {};

    writes[0].descriptors = &image;
    writes[0].type = ac_descriptor_type_uav_image;
    writes[0].count = 1;

    writes[1].descriptors = &tlas;
    writes[1].type = ac_descriptor_type_as;
    writes[1].count = 1;

    ac_update_set(m_db, ac_space0, i, AC_COUNTOF(writes), writes);
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

    ac_destroy_as(m_device, m_blas);
    ac_destroy_as(m_device, m_tlas);

    ac_destroy_buffer(m_vertex_buffer);
    ac_destroy_buffer(m_index_buffer);

    ac_destroy_image(m_output_image);

    ac_destroy_descriptor_buffer(m_db);

    ac_destroy_sbt(m_device, m_sbt);
    ac_destroy_pipeline(m_pipeline);
    ac_destroy_dsl(m_dsl);

    ac_destroy_shader(m_raygen);
    ac_destroy_shader(m_closest_hit);
    ac_destroy_shader(m_miss);

    for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
    {
      ac_destroy_cmd(m_device, m_cmd_pools[i], m_cmds[i]);
      ac_destroy_cmd_pool(m_cmd_pools[i]);
      ac_destroy_fence(m_render_finished_fences[i]);
      ac_destroy_fence(m_acquire_finished_fences[i]);
    }
    ac_destroy_fence(m_fence);

    ac_queue_wait_idle(ac_device_get_queue(m_device, ac_queue_type_graphics));
    ac_destroy_swapchain(m_swapchain);

    ac_destroy_device(m_device);
    m_device = NULL;
  }

  ac_shutdown_window();
  ac_shutdown();
}

ac_result
App::frame()
{
  ac_wait_fence(m_device, m_fence, m_fence_value);

  AC_RIF(ac_acquire_next_image(
    m_device,
    m_swapchain,
    m_acquire_finished_fences[m_frame_index]));

  ac_cmd cmd = m_cmds[m_frame_index];

  AC_RIF(ac_reset_cmd_pool(m_device, m_cmd_pools[m_frame_index]));
  AC_RIF(ac_begin_cmd(cmd));

  {
    ac_image_barrier barriers[1] = {};
    barriers[0].image = m_output_image;
    barriers[0].src_stage = ac_pipeline_stage_none;
    barriers[0].dst_stage = ac_pipeline_stage_compute_shader_bit;
    barriers[0].src_access = ac_access_none;
    barriers[0].dst_access = ac_access_shader_write_bit;
    barriers[0].old_layout = ac_image_layout_undefined;
    barriers[0].new_layout = ac_image_layout_general;
    ac_cmd_barrier(cmd, 0, nullptr, AC_COUNTOF(barriers), barriers);
  }

  ac_cmd_bind_pipeline(cmd, m_pipeline);
  ac_cmd_bind_set(cmd, m_db, ac_space0, 0);
  ac_cmd_trace_rays(
    cmd,
    m_sbt,
    ac_image_get_width(m_output_image),
    ac_image_get_height(m_output_image),
    1);

  ac_image swapchain_image = ac_swapchain_get_image(m_swapchain);

  {
    ac_image_barrier barriers[2] = {};

    barriers[0].image = m_output_image;
    barriers[0].src_stage = ac_pipeline_stage_compute_shader_bit;
    barriers[0].dst_stage = ac_pipeline_stage_transfer_bit;
    barriers[0].src_access = ac_access_shader_write_bit;
    barriers[0].dst_access = ac_access_transfer_read_bit;
    barriers[0].old_layout = ac_image_layout_general;
    barriers[0].new_layout = ac_image_layout_transfer_src;

    barriers[1].image = swapchain_image;
    barriers[1].src_stage = ac_pipeline_stage_none;
    barriers[1].dst_stage = ac_pipeline_stage_transfer_bit;
    barriers[1].src_access = ac_access_none;
    barriers[1].dst_access = ac_access_transfer_write_bit;
    barriers[1].old_layout = ac_image_layout_undefined;
    barriers[1].new_layout = ac_image_layout_transfer_dst;

    ac_cmd_barrier(cmd, 0, nullptr, AC_COUNTOF(barriers), barriers);
  }

  ac_image_copy copy = {};
  copy.width = ac_image_get_width(swapchain_image);
  copy.height = ac_image_get_height(swapchain_image);

  ac_cmd_copy_image(
    cmd,
    m_output_image,
    ac_swapchain_get_image(m_swapchain),
    &copy);

  {
    ac_image_barrier barriers[1] = {};

    barriers[0].image = swapchain_image;
    barriers[0].src_stage = ac_pipeline_stage_transfer_bit;
    barriers[0].dst_stage = ac_pipeline_stage_transfer_bit;
    barriers[0].src_access = ac_access_transfer_write_bit;
    barriers[0].dst_access = ac_access_transfer_read_bit;
    barriers[0].old_layout = ac_image_layout_transfer_dst;
    barriers[0].new_layout = ac_image_layout_present_src;

    ac_cmd_barrier(cmd, 0, nullptr, AC_COUNTOF(barriers), barriers);
  }

  AC_RIF(ac_end_cmd(cmd));

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  m_fence_value++;

  ac_fence_submit_info wait_fence = {};
  wait_fence.fence = m_acquire_finished_fences[m_frame_index];
  ac_fence_submit_info signal_fences[2] = {};
  signal_fences[0].fence = m_render_finished_fences[m_frame_index];
  signal_fences[1].fence = m_fence;
  signal_fences[1].value = m_fence_value;

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  submit_info.wait_fence_count = 1;
  submit_info.wait_fences = &wait_fence;
  submit_info.signal_fence_count = AC_COUNTOF(signal_fences);
  submit_info.signal_fences = signal_fences;

  ac_queue_submit(queue, &submit_info);

  ac_queue_present_info present_info = {};
  present_info.wait_fence_count = 1;
  present_info.wait_fences = &m_render_finished_fences[m_frame_index];
  present_info.swapchain = m_swapchain;

  AC_RIF(ac_queue_present(queue, &present_info));
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

    if (frame() != ac_result_success)
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
  // needed for non ac windows
  swapchain_info.wsi = nullptr;

  AC_RIF(ac_create_swapchain(m_device, &swapchain_info, &m_swapchain));

  ac_raytracing_group_info groups[3] = {};
  groups[0].type = ac_raytracing_group_type_general;
  groups[0].general = 0;
  groups[0].closest_hit = AC_SHADER_UNUSED;
  groups[0].any_hit = AC_SHADER_UNUSED;
  groups[0].intersection = AC_SHADER_UNUSED;

  groups[1].type = ac_raytracing_group_type_general;
  groups[1].general = 1;
  groups[1].closest_hit = AC_SHADER_UNUSED;
  groups[1].any_hit = AC_SHADER_UNUSED;
  groups[1].intersection = AC_SHADER_UNUSED;

  groups[2].type = ac_raytracing_group_type_triangles;
  groups[2].general = AC_SHADER_UNUSED;
  groups[2].closest_hit = 2;
  groups[2].any_hit = AC_SHADER_UNUSED;
  groups[2].intersection = AC_SHADER_UNUSED;

  {
    ac_shader shaders[] = {
      m_raygen,
      m_miss,
      m_closest_hit,
    };

    ac_pipeline_info info = {};
    info.type = ac_pipeline_type_raytracing;

    ac_raytracing_pipeline_info* raytracing = &info.raytracing;
    raytracing->dsl = m_dsl;
    raytracing->shader_count = AC_COUNTOF(shaders);
    raytracing->shaders = shaders;
    raytracing->max_ray_recursion_depth = 1;
    raytracing->group_count = AC_COUNTOF(groups);
    raytracing->groups = groups;

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipeline));
  }

  {
    ac_sbt_info info = {};
    info.pipeline = m_pipeline;
    info.group_count = AC_COUNTOF(groups);
    info.name = AC_DEBUG_NAME("sbt");

    AC_RIF(ac_create_sbt(m_device, &info, &m_sbt));
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
