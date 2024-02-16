#include <vector>
#include <unordered_map>
#include <string.h>
#include <tinyobjloader/tiny_obj_loader.h>
#include <tinygltf/stb_image.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <ac/ac.h>
#include "compiled/main.h"

struct ShaderData {
  glm::mat4 mvp = {};
};

struct TextureData {
  uint32_t             width = {};
  uint32_t             height = {};
  std::vector<uint8_t> data = {};
};

struct Vertex {
  glm::vec3 pos;
  glm::vec2 uv;

  bool
  operator==(const Vertex& other) const
  {
    return (this->pos == other.pos) && (this->uv == other.uv);
  }
};

static inline void
hash_combine(size_t& seed, size_t hash)
{
  hash += 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= hash;
}

namespace std
{
template <>
struct hash<glm::vec2> {
  size_t
  operator()(glm::vec2 const& v) const
  {
    size_t      seed = 0;
    hash<float> hasher;
    hash_combine(seed, hasher(v[0]));
    hash_combine(seed, hasher(v[1]));
    return seed;
  }
};

template <>
struct hash<glm::vec3> {
  size_t
  operator()(glm::vec3 const& v) const
  {
    size_t      seed = 0;
    hash<float> hasher;
    hash_combine(seed, hasher(v[0]));
    hash_combine(seed, hasher(v[1]));
    hash_combine(seed, hasher(v[2]));
    return seed;
  }
};

template <>
struct hash<Vertex> {
  size_t
  operator()(Vertex const& vertex) const
  {
    return ((hash<glm::vec3>()(vertex.pos)) >> 1) ^
           (hash<glm::vec2>()(vertex.uv) << 1);
  }
};
} // namespace std

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
  static constexpr const char* APP_NAME = "02_model";

  enum Token : uint64_t {
    ColorImage = 0,
    DepthImage = 1,
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

  ac_buffer  m_cbv_buffer[AC_MAX_FRAME_IN_FLIGHT] = {};
  ac_buffer  m_vertex_buffer = {};
  ac_buffer  m_index_buffer = {};
  ac_sampler m_sampler = {};
  ac_image   m_image = {};

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

  ac_result
  create_texture(TextureData* texture, ac_image* image);

  ac_result
  load_model(
    std::vector<Vertex>*   vertices,
    std::vector<uint32_t>* indices,
    TextureData*           texture);

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
    info.max_sets[1] = 1;
    info.name = AC_DEBUG_NAME(App::APP_NAME);
    RIF(ac_create_descriptor_buffer(m_device, &info, &m_db));
  }

  for (uint32_t i = 0; i < AC_MAX_FRAME_IN_FLIGHT; ++i)
  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_cbv_bit;
    info.size = sizeof(ShaderData);
    RIF(ac_create_buffer(m_device, &info, &m_cbv_buffer[i]));

    RIF(ac_buffer_map_memory(m_cbv_buffer[i]));

    ac_descriptor descriptor = {};
    descriptor.buffer = m_cbv_buffer[i];
    ac_descriptor_write write = {};
    write.count = 1;
    write.type = ac_descriptor_type_cbv_buffer;
    write.descriptors = &descriptor;
    ac_update_set(m_db, ac_space0, i, 1, &write);
  }

  std::vector<Vertex>   vertices;
  std::vector<uint32_t> indices;
  TextureData           texture;
  RIF(load_model(&vertices, &indices, &texture));

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_vertex_bit;
    info.size = vertices.size() * sizeof(vertices[0]);
    info.name = AC_DEBUG_NAME("vb");

    RIF(ac_create_buffer(m_device, &info, &m_vertex_buffer));
    RIF(ac_buffer_map_memory(m_vertex_buffer));
    memcpy(
      ac_buffer_get_mapped_memory(m_vertex_buffer),
      vertices.data(),
      vertices.size() * sizeof(vertices[0]));
    ac_buffer_unmap_memory(m_vertex_buffer);
  }

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_cpu_to_gpu;
    info.usage = ac_buffer_usage_index_bit;
    info.size = indices.size() * sizeof(indices[0]);
    info.name = AC_DEBUG_NAME("ib");

    RIF(ac_create_buffer(m_device, &info, &m_index_buffer));
    RIF(ac_buffer_map_memory(m_index_buffer));

    memcpy(
      ac_buffer_get_mapped_memory(m_index_buffer),
      indices.data(),
      indices.size() * sizeof(indices[0]));
    ac_buffer_unmap_memory(m_index_buffer);
  }

  RIF(create_texture(&texture, &m_image));

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

  ac_descriptor sampler_ds = {};
  sampler_ds.sampler = m_sampler;
  ac_descriptor image_ds = {};
  image_ds.image = m_image;

  ac_descriptor_write writes[2] = {};
  writes[0].type = ac_descriptor_type_sampler;
  writes[0].count = 1;
  writes[0].descriptors = &sampler_ds;
  writes[1].type = ac_descriptor_type_srv_image;
  writes[1].count = 1;
  writes[1].descriptors = &image_ds;

  ac_update_set(m_db, ac_space1, 0, AC_COUNTOF(writes), writes);

  glm::vec3 eye = {2.0f, 2.0f, 2.0f};
  glm::vec3 origin = {0.0f, 0.0f, 0.0f};
  glm::vec3 up = {0.0f, 0.0f, 1.0f};

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

    ac_destroy_image(m_image);
    ac_destroy_sampler(m_sampler);
    ac_destroy_buffer(m_vertex_buffer);
    ac_destroy_buffer(m_index_buffer);

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

  {
    ac_destroy_pipeline(m_pipeline);

    ac_vertex_layout vl = {};
    vl.binding_count = 1;
    vl.bindings[0].stride = sizeof(Vertex);
    vl.attribute_count = 2;
    vl.attributes[0].format = ac_format_r32g32b32_sfloat;
    vl.attributes[0].semantic = ac_attribute_semantic_position;
    vl.attributes[0].offset = AC_OFFSETOF(Vertex, pos);
    vl.attributes[1].format = ac_format_r32g32_sfloat;
    vl.attributes[1].semantic = ac_attribute_semantic_texcoord0;
    vl.attributes[1].offset = AC_OFFSETOF(Vertex, uv);

    ac_depth_state_info depth_state = {};
    depth_state.depth_test = true;
    depth_state.depth_write = true;
    depth_state.compare_op = ac_compare_op_less;

    ac_rasterizer_state_info rasterizer_state = {};
    rasterizer_state.cull_mode = ac_cull_mode_back;
    rasterizer_state.front_face = ac_front_face_counter_clockwise;
    rasterizer_state.polygon_mode = ac_polygon_mode_fill;

    ac_image image = ac_swapchain_get_image(m_swapchain);

    ac_pipeline_info info = {};
    info.name = AC_DEBUG_NAME(App::APP_NAME);
    info.type = ac_pipeline_type_graphics;
    info.graphics.vertex_shader = m_vertex_shader;
    info.graphics.pixel_shader = m_fragment_shader;
    info.graphics.dsl = m_dsl;
    info.graphics.vertex_layout = vl;
    info.graphics.topology = ac_primitive_topology_triangle_list;
    info.graphics.samples = 1;
    info.graphics.color_attachment_count = 1;
    info.graphics.color_attachment_formats[0] = ac_image_get_format(image);
    info.graphics.depth_stencil_format = ac_format_d32_sfloat;
    info.graphics.depth_state_info = depth_state;
    info.graphics.rasterizer_info = rasterizer_state;

    AC_RIF(ac_create_pipeline(m_device, &info, &m_pipeline));
  }

  return ac_result_success;
}

ac_result
App::stage_prepare(ac_rg_stage* stage, void* ud)
{
  App* p = static_cast<App*>(ud);

  glm::mat4 mvp, model, vp;

  vp = p->m_matrices.projection * p->m_matrices.view;
  model = p->m_matrices.model;
  p->m_matrices.model = glm::rotate(model, 0.01f, glm::vec3(0.0, 0.0, 1.0));
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

  ac_cmd   cmd = stage->cmd;
  ac_image image = ac_swapchain_get_image(p->m_swapchain);
  uint32_t width = ac_image_get_width(image);
  uint32_t height = ac_image_get_height(image);

  ac_cmd_set_viewport(cmd, 0, 0, (float)width, (float)height, 0.0f, 1.0f);
  ac_cmd_set_scissor(cmd, 0, 0, width, height);

  ac_cmd_bind_pipeline(cmd, p->m_pipeline);
  ac_cmd_bind_set(cmd, p->m_db, ac_space0, stage->frame);
  ac_cmd_bind_set(cmd, p->m_db, ac_space1, 0);
  ac_cmd_bind_vertex_buffer(cmd, 0, p->m_vertex_buffer, 0);
  ac_cmd_bind_index_buffer(cmd, p->m_index_buffer, 0, ac_index_type_u32);
  ac_cmd_draw_indexed(
    cmd,
    static_cast<uint32_t>(
      ac_buffer_get_size(p->m_index_buffer) / sizeof(uint32_t)),
    1,
    0,
    0,
    0);

  return ac_result_success;
}

ac_result
App::build_frame(ac_rg_builder builder, void* ud)
{
  App* p = static_cast<App*>(ud);

  ac_image      image = ac_swapchain_get_image(p->m_swapchain);
  ac_image_info color = ac_image_get_info(image);
  color.clear_value = {{{0.580, 0.659, 0.604, 1.0}}};
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
App::create_texture(TextureData* texture, ac_image* image)
{
  ac_image_info image_info = {};
  image_info.width = texture->width;
  image_info.height = texture->height;
  image_info.format = ac_format_r8g8b8a8_srgb;
  image_info.layers = 1;
  image_info.levels = 1;
  image_info.samples = 1;
  image_info.type = ac_image_type_2d;
  image_info.usage = ac_image_usage_srv_bit | ac_image_usage_transfer_dst_bit;
  image_info.name = AC_DEBUG_NAME("texture");

  AC_RIF(ac_create_image(m_device, &image_info, image));

  ac_device_properties props = ac_device_get_properties(m_device);

  uint64_t pixel_size = ac_format_size_bytes(ac_image_get_format(*image));

  uint64_t src_row_size = (texture->width * pixel_size);
  uint64_t dst_row_size = AC_ALIGN_UP(src_row_size, props.image_row_alignment);
  uint64_t image_size =
    AC_ALIGN_UP(dst_row_size * texture->height, props.image_alignment);

  ac_buffer_info buffer_info = {};
  buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
  buffer_info.size = image_size;
  buffer_info.usage = ac_buffer_usage_transfer_src_bit;
  buffer_info.name = AC_DEBUG_NAME("staging buffer");

  ac_buffer staging_buffer;
  AC_RIF(ac_create_buffer(m_device, &buffer_info, &staging_buffer));
  AC_RIF(ac_buffer_map_memory(staging_buffer));

  const uint8_t* src = texture->data.data();
  uint8_t*       dst = (uint8_t*)ac_buffer_get_mapped_memory(staging_buffer);

  for (uint32_t i = 0; i < texture->height; ++i)
  {
    memcpy(dst, src, src_row_size);
    dst += dst_row_size;
    src += src_row_size;
  }

  ac_buffer_unmap_memory(staging_buffer);

  ac_queue queue = ac_device_get_queue(m_device, ac_queue_type_graphics);

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = queue;

  ac_cmd_pool pool;
  AC_RIF(ac_create_cmd_pool(m_device, &pool_info, &pool));

  ac_cmd cmd;
  AC_RIF(ac_create_cmd(pool, &cmd));

  ac_begin_cmd(cmd);

  {
    ac_image_barrier barrier = {};
    barrier.src_access = ac_access_none;
    barrier.dst_access = ac_access_transfer_write_bit;
    barrier.old_layout = ac_image_layout_undefined;
    barrier.new_layout = ac_image_layout_transfer_dst;
    barrier.image = *image;
    barrier.src_stage = ac_pipeline_stage_top_of_pipe_bit;
    barrier.dst_stage = ac_pipeline_stage_transfer_bit;

    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  ac_buffer_image_copy copy = {};
  copy.width = texture->width;
  copy.height = texture->height;

  ac_cmd_copy_buffer_to_image(cmd, staging_buffer, *image, &copy);

  {
    ac_image_barrier barrier = {};
    barrier.src_access = ac_access_transfer_write_bit;
    barrier.dst_access = ac_access_shader_read_bit;
    barrier.old_layout = ac_image_layout_transfer_dst;
    barrier.new_layout = ac_image_layout_shader_read;
    barrier.image = *image;
    barrier.src_stage = ac_pipeline_stage_transfer_bit;
    barrier.dst_stage = ac_pipeline_stage_pixel_shader_bit;
    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  ac_end_cmd(cmd);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  AC_RIF(ac_queue_submit(queue, &submit_info));
  AC_RIF(ac_queue_wait_idle(queue));

  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);
  ac_destroy_buffer(staging_buffer);

  return ac_result_success;
}

ac_result
App::load_model(
  std::vector<Vertex>*   vertices,
  std::vector<uint32_t>* indices,
  TextureData*           texture)
{
  std::vector<uint8_t> obj;
  {
    ac_file file;
    AC_RIF(ac_create_file(
      AC_SYSTEM_FS,
      ac_mount_rom,
      "viking_room.model",
      ac_file_mode_read_bit,
      &file));

    obj.resize(ac_file_get_size(file));
    AC_RIF(ac_file_read(file, obj.size(), obj.data()));

    ac_destroy_file(file);
  }
  {
    ac_file file;
    AC_RIF(ac_create_file(
      AC_SYSTEM_FS,
      ac_mount_rom,
      "viking_room.png",
      ac_file_mode_read_bit,
      &file));

    std::vector<uint8_t> file_data;
    file_data.resize(ac_file_get_size(file));
    AC_RIF(ac_file_read(file, file_data.size(), file_data.data()));

    ac_destroy_file(file);

    int      x, y, ch;
    stbi_uc* image_data = stbi_load_from_memory(
      file_data.data(),
      (int32_t)file_data.size(),
      &x,
      &y,
      &ch,
      STBI_rgb_alpha);

    texture->width = x;
    texture->height = y;
    texture->data.insert(
      texture->data.cbegin(),
      image_data,
      image_data + (texture->width * texture->height * 4));

    stbi_image_free(image_data);
  }

  tinyobj::ObjReader reader;
  if (!reader.ParseFromString(std::string(obj.begin(), obj.end()), {}))
  {
    AC_ERROR("failed to parse obj");
    return ac_result_unknown_error;
  }

  tinyobj::attrib_t                attrib = reader.GetAttrib();
  std::vector<tinyobj::shape_t>    shapes = reader.GetShapes();
  std::vector<tinyobj::material_t> materials = reader.GetMaterials();

  std::unordered_map<Vertex, uint32_t> unique_vertices {};

  for (const auto& shape : shapes)
  {
    for (const auto& index : shape.mesh.indices)
    {
      glm::vec3 pos = {
        attrib.vertices[3 * index.vertex_index + 0],
        attrib.vertices[3 * index.vertex_index + 1],
        attrib.vertices[3 * index.vertex_index + 2],
      };

      glm::vec2 uv = {
        attrib.texcoords[2 * index.texcoord_index + 0],
        1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
      };

      Vertex vertex {};
      vertex.pos = pos;
      vertex.uv = uv;

      if (unique_vertices.count(vertex) == 0)
      {
        unique_vertices[vertex] = static_cast<uint32_t>(vertices->size());
        vertices->push_back(vertex);
      }

      indices->push_back(unique_vertices[vertex]);
    }
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
