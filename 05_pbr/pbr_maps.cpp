#include <math.h>
#include <string.h>
#include <tinygltf/stb_image.h>
#include "pbr_maps.hpp"

#include "compiled/brdf.h"
#include "compiled/eq_to_cube.h"
#include "compiled/irradiance.h"
#include "compiled/specular.h"

ac_result
compute_pbr_maps2(ac_device device, ac_image equirectangular, PBRMaps* maps)
{
  const uint32_t BRDF_INTEGRATION_SIZE = 512;
  const uint32_t SKYBOX_SIZE = 1024;
  const uint32_t SKYBOX_MIPS = (uint32_t)log2(SKYBOX_SIZE) + 1;
  const uint32_t IRRADIANCE_SIZE = 64;
  const uint32_t SPECULAR_SIZE = 512;
  const uint32_t SPECULAR_MIPS = (uint32_t)log2(SPECULAR_SIZE) + 1;

  ac_shader            eq_to_cube_shader = NULL;
  ac_dsl               eq_to_cube_dsl = NULL;
  ac_descriptor_buffer eq_to_cube_db = NULL;
  ac_pipeline          eq_to_cube_pipeline = NULL;
  ac_shader            brdf_integration_shader = NULL;
  ac_dsl               brdf_integration_dsl = NULL;
  ac_descriptor_buffer brdf_integration_db = NULL;
  ac_pipeline          brdf_integration_pipeline = NULL;
  ac_shader            irradiance_shader = NULL;
  ac_dsl               irradiance_dsl = NULL;
  ac_descriptor_buffer irradiance_db = NULL;
  ac_pipeline          irradiance_pipeline = NULL;
  ac_shader            specular_shader = NULL;
  ac_dsl               specular_dsl = NULL;
  ac_descriptor_buffer specular_db = NULL;
  ac_pipeline          specular_pipeline = NULL;
  ac_sampler           skybox_sampler = NULL;

  ac_sampler_info sampler_info = {};
  sampler_info.mag_filter = ac_filter_linear;
  sampler_info.min_filter = ac_filter_linear;
  sampler_info.mipmap_mode = ac_sampler_mipmap_mode_linear;
  sampler_info.address_mode_u = ac_sampler_address_mode_repeat;
  sampler_info.address_mode_v = ac_sampler_address_mode_repeat;
  sampler_info.address_mode_w = ac_sampler_address_mode_repeat;
  sampler_info.anisotropy_enable = true;
  sampler_info.max_anisotropy = 16;
  sampler_info.min_lod = 0.0;
  sampler_info.max_lod = 16.0f;

  AC_RIF(ac_create_sampler(device, &sampler_info, &skybox_sampler));

  ac_image_info skybox_info = {};
  skybox_info.width = SKYBOX_SIZE;
  skybox_info.height = SKYBOX_SIZE;
  skybox_info.levels = SKYBOX_MIPS;
  skybox_info.layers = 6;
  skybox_info.format = ac_format_r32g32b32a32_sfloat;
  skybox_info.samples = 1;
  skybox_info.usage = ac_image_usage_srv_bit | ac_image_usage_uav_bit |
                      ac_image_usage_transfer_dst_bit;
  skybox_info.type = ac_image_type_cube;
  skybox_info.name = "skybox image";

  AC_RIF(ac_create_image(device, &skybox_info, &maps->environment));

  ac_image_info irr_info = {};
  irr_info.width = IRRADIANCE_SIZE;
  irr_info.height = IRRADIANCE_SIZE;
  irr_info.levels = 1;
  irr_info.layers = 6;
  irr_info.format = ac_format_r32g32b32a32_sfloat;
  irr_info.samples = 1;
  irr_info.usage = ac_image_usage_srv_bit | ac_image_usage_uav_bit |
                   ac_image_usage_transfer_dst_bit;
  irr_info.type = ac_image_type_cube;
  irr_info.name = "irradiance image";

  AC_RIF(ac_create_image(device, &irr_info, &maps->irradiance));

  ac_image_info spec_info = {};
  spec_info.width = SPECULAR_SIZE;
  spec_info.height = SPECULAR_SIZE;
  spec_info.levels = SPECULAR_MIPS;
  spec_info.layers = 6;
  spec_info.format = ac_format_r32g32b32a32_sfloat;
  spec_info.samples = 1;
  spec_info.usage = ac_image_usage_srv_bit | ac_image_usage_uav_bit |
                    ac_image_usage_transfer_dst_bit;
  spec_info.type = ac_image_type_cube;
  spec_info.name = "spec image";

  AC_RIF(ac_create_image(device, &spec_info, &maps->specular));

  ac_image_info brdf_info = {};
  brdf_info.width = BRDF_INTEGRATION_SIZE;
  brdf_info.height = BRDF_INTEGRATION_SIZE;
  brdf_info.levels = 1;
  brdf_info.layers = 1;
  brdf_info.format = ac_format_r32g32_sfloat;
  brdf_info.samples = 1;
  brdf_info.usage = ac_image_usage_srv_bit | ac_image_usage_uav_bit |
                    ac_image_usage_transfer_dst_bit;
  brdf_info.type = ac_image_type_2d;
  brdf_info.name = "brdf image";

  AC_RIF(ac_create_image(device, &brdf_info, &maps->brdf));

  ac_shader_info shader_info = {};
  shader_info.stage = ac_shader_stage_compute;
  shader_info.code = eq_to_cube_cs[0];
  AC_RIF(ac_create_shader(device, &shader_info, &eq_to_cube_shader));
  shader_info.code = brdf_cs[0];
  AC_RIF(ac_create_shader(device, &shader_info, &brdf_integration_shader));
  shader_info.code = irradiance_cs[0];
  AC_RIF(ac_create_shader(device, &shader_info, &irradiance_shader));
  shader_info.code = specular_cs[0];
  AC_RIF(ac_create_shader(device, &shader_info, &specular_shader));

  ac_dsl_info dsl_info = {};
  dsl_info.shader_count = 1;
  dsl_info.shaders = &eq_to_cube_shader;
  AC_RIF(ac_create_dsl(device, &dsl_info, &eq_to_cube_dsl));
  dsl_info.shaders = &brdf_integration_shader;
  AC_RIF(ac_create_dsl(device, &dsl_info, &brdf_integration_dsl));
  dsl_info.shaders = &irradiance_shader;
  AC_RIF(ac_create_dsl(device, &dsl_info, &irradiance_dsl));
  dsl_info.shaders = &specular_shader;
  AC_RIF(ac_create_dsl(device, &dsl_info, &specular_dsl));

  ac_descriptor_buffer_info db_info = {};
  db_info.max_sets[ac_space0] = SKYBOX_MIPS;
  db_info.dsl = eq_to_cube_dsl;
  AC_RIF(ac_create_descriptor_buffer(device, &db_info, &eq_to_cube_db));
  db_info.max_sets[ac_space0] = 1;
  db_info.dsl = brdf_integration_dsl;
  AC_RIF(ac_create_descriptor_buffer(device, &db_info, &brdf_integration_db));
  db_info.max_sets[ac_space0] = 1;
  db_info.dsl = irradiance_dsl;
  AC_RIF(ac_create_descriptor_buffer(device, &db_info, &irradiance_db));
  db_info.max_sets[0] = SPECULAR_MIPS;
  db_info.dsl = specular_dsl;
  AC_RIF(ac_create_descriptor_buffer(device, &db_info, &specular_db));

  ac_pipeline_info pipe_info = {};
  pipe_info.type = ac_pipeline_type_compute;
  pipe_info.compute.dsl = eq_to_cube_dsl;
  pipe_info.compute.shader = eq_to_cube_shader;
  AC_RIF(ac_create_pipeline(device, &pipe_info, &eq_to_cube_pipeline));
  pipe_info.compute.dsl = irradiance_dsl;
  pipe_info.compute.shader = irradiance_shader;
  AC_RIF(ac_create_pipeline(device, &pipe_info, &irradiance_pipeline));
  pipe_info.compute.dsl = specular_dsl;
  pipe_info.compute.shader = specular_shader;
  AC_RIF(ac_create_pipeline(device, &pipe_info, &specular_pipeline));
  pipe_info.compute.dsl = brdf_integration_dsl;
  pipe_info.compute.shader = brdf_integration_shader;
  AC_RIF(ac_create_pipeline(device, &pipe_info, &brdf_integration_pipeline));

  {
    ac_descriptor       ds[1] = {};
    ac_descriptor_write ws[1] = {};
    ds[0].image = maps->brdf;
    ws[0].count = 1;
    ws[0].type = ac_descriptor_type_uav_image;
    ws[0].descriptors = &ds[0];
    ac_update_set(brdf_integration_db, ac_space0, 0, 1, ws);
  }

  for (uint32_t i = 0; i < SKYBOX_MIPS; ++i)
  {
    ac_descriptor       ds[3] = {};
    ac_descriptor_write ws[3] = {};
    ds[0].sampler = skybox_sampler;
    ws[0].count = 1;
    ws[0].type = ac_descriptor_type_sampler;
    ws[0].descriptors = &ds[0];
    ds[1].image = equirectangular;
    ws[1].count = 1;
    ws[1].type = ac_descriptor_type_srv_image;
    ws[1].descriptors = &ds[1];
    ds[2].image = maps->environment;
    ds[2].level = i;
    ws[2].count = 1;
    ws[2].type = ac_descriptor_type_uav_image;
    ws[2].descriptors = &ds[2];
    ac_update_set(eq_to_cube_db, ac_space0, i, 3, ws);
  }

  {
    ac_descriptor       ds[3] = {};
    ac_descriptor_write ws[3] = {};
    ds[0].sampler = skybox_sampler;
    ws[0].count = 1;
    ws[0].type = ac_descriptor_type_sampler;
    ws[0].descriptors = &ds[0];
    ds[1].image = maps->environment;
    ws[1].count = 1;
    ws[1].type = ac_descriptor_type_srv_image;
    ws[1].descriptors = &ds[1];
    ds[2].image = maps->irradiance;
    ws[2].count = 1;
    ws[2].type = ac_descriptor_type_uav_image;
    ws[2].descriptors = &ds[2];
    ac_update_set(irradiance_db, ac_space0, 0, 3, ws);
  }

  for (uint32_t i = 0; i < SPECULAR_MIPS; ++i)
  {
    ac_descriptor       ds[3] = {};
    ac_descriptor_write ws[3] = {};
    ds[0].sampler = skybox_sampler;
    ws[0].count = 1;
    ws[0].type = ac_descriptor_type_sampler;
    ws[0].descriptors = &ds[0];
    ds[1].image = maps->environment;
    ws[1].count = 1;
    ws[1].type = ac_descriptor_type_srv_image;
    ws[1].descriptors = &ds[1];
    ds[2].image = maps->specular;
    ds[2].level = i;
    ws[2].count = 1;
    ws[2].type = ac_descriptor_type_uav_image;
    ws[2].descriptors = &ds[2];
    ac_update_set(specular_db, ac_space0, i, 3, ws);
  }

  ac_queue queue = ac_device_get_queue(device, ac_queue_type_compute);

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = queue;

  ac_cmd_pool pool;
  AC_RIF(ac_create_cmd_pool(device, &pool_info, &pool));

  ac_cmd cmd = NULL;
  AC_RIF(ac_create_cmd(pool, &cmd));

  ac_begin_cmd(cmd);

  ac_image_barrier barriers[4] = {};
  barriers[0].image = maps->brdf;
  barriers[1].image = maps->environment;
  barriers[2].image = maps->irradiance;
  barriers[3].image = maps->specular;

  for (uint32_t i = 0; i < AC_COUNTOF(barriers); ++i)
  {
    ac_image_barrier* b = &barriers[i];
    b->src_access = ac_access_none;
    b->dst_access = ac_access_shader_read_bit | ac_access_shader_write_bit;
    b->old_layout = ac_image_layout_undefined;
    b->new_layout = ac_image_layout_general;
    b->src_stage = ac_pipeline_stage_top_of_pipe_bit;
    b->dst_stage = ac_pipeline_stage_compute_shader_bit;
  }
  ac_cmd_barrier(cmd, 0, NULL, AC_COUNTOF(barriers), barriers);

  {
    ac_cmd_bind_pipeline(cmd, brdf_integration_pipeline);
    ac_cmd_bind_set(cmd, brdf_integration_db, ac_space0, 0);

    uint8_t wg[3];
    AC_RIF(ac_shader_get_workgroup(brdf_integration_shader, wg));

    ac_cmd_dispatch(
      cmd,
      BRDF_INTEGRATION_SIZE / wg[0],
      BRDF_INTEGRATION_SIZE / wg[1],
      wg[2]);
  }

  {
    ac_image_barrier* b = &barriers[0];
    b->src_access = ac_access_shader_read_bit | ac_access_shader_write_bit;
    b->dst_access = ac_access_shader_read_bit;
    b->old_layout = ac_image_layout_general;
    b->new_layout = ac_image_layout_shader_read;
    b->src_stage = ac_pipeline_stage_compute_shader_bit;
    b->dst_stage = ac_pipeline_stage_compute_shader_bit;
    b->image = maps->brdf;
    ac_cmd_barrier(cmd, 0, NULL, 1, b);
  }
  ac_cmd_bind_pipeline(cmd, eq_to_cube_pipeline);

  struct {
    uint32_t mip;
    uint32_t textureSize;
  } pc = {0, SKYBOX_SIZE};

  for (uint32_t i = 0; i < SKYBOX_MIPS; ++i)
  {
    pc.mip = i;
    ac_cmd_push_constants(cmd, sizeof(pc), &pc);

    ac_cmd_bind_set(cmd, eq_to_cube_db, ac_space0, i);

    uint8_t wg[3];
    AC_RIF(ac_shader_get_workgroup(eq_to_cube_shader, wg));

    ac_cmd_dispatch(
      cmd,
      AC_MAX(1u, (pc.textureSize >> i) / wg[0]),
      AC_MAX(1u, (pc.textureSize >> i) / wg[1]),
      6 / wg[2]);
  }

  {
    ac_image_barrier* b = &barriers[0];
    b->src_access = ac_access_shader_read_bit | ac_access_shader_write_bit;
    b->dst_access = ac_access_shader_read_bit;
    b->old_layout = ac_image_layout_general;
    b->new_layout = ac_image_layout_shader_read;
    b->src_stage = ac_pipeline_stage_compute_shader_bit;
    b->dst_stage = ac_pipeline_stage_compute_shader_bit;
    b->image = maps->environment;
    ac_cmd_barrier(cmd, 0, NULL, 1, b);
  }

  {
    ac_cmd_bind_pipeline(cmd, irradiance_pipeline);
    ac_cmd_bind_set(cmd, irradiance_db, ac_space0, 0);

    uint8_t wg[3];
    AC_RIF(ac_shader_get_workgroup(irradiance_shader, wg));

    ac_cmd_dispatch(
      cmd,
      IRRADIANCE_SIZE / wg[0],
      IRRADIANCE_SIZE / wg[1],
      6 / wg[2]);
  }

  struct {
    uint32_t mip_size;
    float    roughness;
  } pc1;

  ac_cmd_bind_pipeline(cmd, specular_pipeline);

  for (uint32_t i = 0; i < SPECULAR_MIPS; i++)
  {
    pc1.roughness = (float)i / (float)(SPECULAR_MIPS - 1);
    pc1.mip_size = (SPECULAR_SIZE >> i);
    ac_cmd_push_constants(cmd, sizeof(pc1), &pc1);

    ac_cmd_bind_set(cmd, specular_db, ac_space0, i);

    uint8_t wg[3];
    AC_RIF(ac_shader_get_workgroup(specular_shader, wg));

    ac_cmd_dispatch(
      cmd,
      AC_MAX(1u, (SPECULAR_SIZE >> i) / wg[0]),
      AC_MAX(1u, (SPECULAR_SIZE >> i) / wg[1]),
      6 / wg[2]);
  }

  {
    ac_image_barrier* b = &barriers[0];
    b->src_access = ac_access_shader_read_bit | ac_access_shader_write_bit;
    b->dst_access = ac_access_shader_read_bit;
    b->old_layout = ac_image_layout_general;
    b->new_layout = ac_image_layout_shader_read;
    b->src_stage = ac_pipeline_stage_compute_shader_bit;
    b->dst_stage = ac_pipeline_stage_compute_shader_bit;
    b->image = maps->irradiance;
    barriers[1] = *b;
    b = &barriers[1];
    b->image = maps->specular;

    ac_cmd_barrier(cmd, 0, NULL, 2, barriers);
  }

  ac_end_cmd(cmd);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  ac_queue_submit(queue, &submit_info);
  ac_queue_wait_idle(queue);

  ac_destroy_pipeline(specular_pipeline);
  ac_destroy_descriptor_buffer(specular_db);
  ac_destroy_dsl(specular_dsl);
  ac_destroy_shader(specular_shader);
  ac_destroy_pipeline(irradiance_pipeline);
  ac_destroy_descriptor_buffer(irradiance_db);
  ac_destroy_dsl(irradiance_dsl);
  ac_destroy_shader(irradiance_shader);
  ac_destroy_pipeline(eq_to_cube_pipeline);
  ac_destroy_descriptor_buffer(eq_to_cube_db);
  ac_destroy_dsl(eq_to_cube_dsl);
  ac_destroy_shader(eq_to_cube_shader);
  ac_destroy_pipeline(brdf_integration_pipeline);
  ac_destroy_descriptor_buffer(brdf_integration_db);
  ac_destroy_dsl(brdf_integration_dsl);
  ac_destroy_shader(brdf_integration_shader);

  ac_destroy_sampler(skybox_sampler);

  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);

  return ac_result_success;
}

ac_result
compute_pbr_maps(ac_device device, std::string filename, PBRMaps* maps)
{
  void* image_data = NULL;

  ac_buffer staging_buffer = NULL;
  ac_image  equirectangular = NULL;

  size_t   file_length = 0;
  uint8_t* file_data = NULL;

  {
    ac_file file;
    AC_RIF(ac_create_file(
      AC_SYSTEM_FS,
      ac_mount_rom,
      filename.c_str(),
      ac_file_mode_read_bit,
      &file));

    file_length = ac_file_get_size(file);
    file_data = (uint8_t*)ac_alloc(file_length);
    AC_RIF(ac_file_read(file, file_length, file_data));

    ac_destroy_file(file);
  }

  {
    int w, h, ch;
    image_data = stbi_loadf_from_memory(
      file_data,
      (int)file_length,
      &w,
      &h,
      &ch,
      STBI_rgb_alpha);

    ac_image_info image_info = {};
    image_info.width = (uint32_t)w;
    image_info.height = (uint32_t)h;
    image_info.format = ac_format_r32g32b32a32_sfloat;
    image_info.layers = 1;
    image_info.levels = 1;
    image_info.samples = 1;
    image_info.usage = ac_image_usage_srv_bit | ac_image_usage_transfer_dst_bit;
    image_info.type = ac_image_type_2d;
    image_info.name = "equirectangular";
    ac_create_image(device, &image_info, &equirectangular);

    ac_device_properties props = ac_device_get_properties(device);

    uint64_t pixel_size =
      ac_format_size_bytes(ac_image_get_format(equirectangular));

    uint64_t src_row_size = (w * pixel_size);
    uint64_t dst_row_size =
      AC_ALIGN_UP(src_row_size, props.image_row_alignment);
    uint64_t image_size = AC_ALIGN_UP(dst_row_size * h, props.image_alignment);

    ac_buffer_info buffer_info = {};
    buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
    buffer_info.size = image_size;
    buffer_info.usage = ac_buffer_usage_transfer_src_bit;
    buffer_info.name = "staging buffer";

    AC_RIF(ac_create_buffer(device, &buffer_info, &staging_buffer));
    ac_buffer_map_memory(staging_buffer);

    const uint8_t* src = (const uint8_t*)image_data;
    uint8_t*       dst = (uint8_t*)ac_buffer_get_mapped_memory(staging_buffer);

    for (int32_t i = 0; i < h; ++i)
    {
      memcpy(dst, src, src_row_size);
      dst += dst_row_size;
      src += src_row_size;
    }

    ac_buffer_unmap_memory(staging_buffer);
  }

  ac_queue queue = ac_device_get_queue(device, ac_queue_type_compute);

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = queue;

  ac_cmd_pool pool;
  AC_RIF(ac_create_cmd_pool(device, &pool_info, &pool));

  ac_cmd cmd = NULL;
  AC_RIF(ac_create_cmd(pool, &cmd));

  ac_begin_cmd(cmd);

  {
    ac_image_barrier barrier = {};
    barrier.src_access = ac_access_none;
    barrier.dst_access = ac_access_transfer_write_bit;
    barrier.src_stage = ac_pipeline_stage_all_commands_bit;
    barrier.dst_stage = ac_pipeline_stage_all_commands_bit;
    barrier.old_layout = ac_image_layout_undefined;
    barrier.new_layout = ac_image_layout_transfer_dst;
    barrier.image = equirectangular;

    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  {
    ac_buffer_image_copy buffer_image_copy = {};

    buffer_image_copy.width = ac_image_get_width(equirectangular);
    buffer_image_copy.height = ac_image_get_height(equirectangular);
    ac_cmd_copy_buffer_to_image(
      cmd,
      staging_buffer,
      equirectangular,
      &buffer_image_copy);
  }

  {
    ac_image_barrier barrier = {};
    barrier.src_access = ac_access_transfer_write_bit;
    barrier.dst_access = ac_access_shader_read_bit;
    barrier.src_stage = ac_pipeline_stage_all_commands_bit;
    barrier.dst_stage = ac_pipeline_stage_all_commands_bit;
    barrier.old_layout = ac_image_layout_transfer_dst;
    barrier.new_layout = ac_image_layout_shader_read;
    barrier.image = equirectangular;

    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  ac_end_cmd(cmd);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  ac_queue_submit(queue, &submit_info);
  ac_queue_wait_idle(queue);

  compute_pbr_maps2(device, equirectangular, maps);

  ac_destroy_image(equirectangular);
  ac_destroy_buffer(staging_buffer);
  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);

  stbi_image_free(image_data);
  ac_free(file_data);

  return ac_result_success;
}
