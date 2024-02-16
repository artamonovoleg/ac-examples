#pragma once

#include <iostream>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include <ac/ac.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE

#include <tinygltf/tiny_gltf.h>

#define MAX_NUM_JOINTS 128u

struct Node;

struct BoundingBox {
  glm::vec3 min;
  glm::vec3 max;
  bool      valid = false;

  BoundingBox();
  BoundingBox(glm::vec3 min, glm::vec3 max);

  BoundingBox
  get_aabb(glm::mat4 m);
};

struct TextureSampler {
  ac_filter               mag_filter;
  ac_filter               min_filter;
  ac_sampler_address_mode address_mode_u;
  ac_sampler_address_mode address_mode_v;
  ac_sampler_address_mode address_mode_w;
};

struct Texture {
  ac_device device;
  ac_image  image;

  void
  destroy();

  ac_result
  from_gltf_image(
    tinygltf::Image& gltf_image,
    TextureSampler   texture_sampler,
    ac_device        device,
    ac_queue         copy_queue);
};

struct Material {
  enum AlphaMode {
    ALPHAMODE_OPAQUE,
    ALPHAMODE_MASK,
    ALPHAMODE_BLEND
  };
  AlphaMode alpha_mode = ALPHAMODE_OPAQUE;
  float     alpha_cutoff = 1.0f;
  float     metallic_factor = 1.0f;
  float     roughness_factor = 1.0f;
  float     emissive_strength = 1.0f;
  glm::vec4 base_color_factor = glm::vec4(1.0f);
  glm::vec4 emissive_factor = glm::vec4(0.0f);
  Texture*  base_color_texture;
  Texture*  metallic_roughness_texture;
  Texture*  normal_texture;
  Texture*  occlusion_texture;
  Texture*  emissive_texture;
  bool      double_sided = false;
  struct TexCoordSets {
    uint8_t base_color = 0;
    uint8_t metallic_roughness = 0;
    uint8_t specular_glossiness = 0;
    uint8_t normal = 0;
    uint8_t occlusion = 0;
    uint8_t emissive = 0;
  } tex_coord_sets;
  struct Extension {
    Texture*  specular_glossiness_texture;
    Texture*  diffuse_texture;
    glm::vec4 diffuse_factor = glm::vec4(1.0f);
    glm::vec3 specular_factor = glm::vec3(0.0f);
  } extension;
  struct PbrWorkflows {
    bool metallic_roughness = true;
    bool specular_glossiness = false;
  } pbr_workflows;
  uint32_t index;
};

struct Primitive {
  uint32_t    first_index;
  uint32_t    index_count;
  uint32_t    vertex_count;
  Material&   material;
  bool        has_indices;
  BoundingBox bb;

  Primitive(
    uint32_t  first_index,
    uint32_t  index_count,
    uint32_t  vertex_count,
    Material& material);

  void
  set_bounding_box(glm::vec3 min, glm::vec3 max);
};

struct Model;
struct Mesh {
  ac_device               device;
  std::vector<Primitive*> primitives;
  BoundingBox             bb;
  BoundingBox             aabb;
  ac_buffer               uniform_buffer;
  uint32_t                node;

  struct UniformBlock {
    glm::mat4 matrix {};
    glm::mat4 joint_matrix[MAX_NUM_JOINTS] {};
    float     joint_count {0};
  };

  UniformBlock* uniform_block;

  Mesh(Model*, glm::mat4 matrix);
  ~Mesh();

  void
  set_bounding_box(glm::vec3 min, glm::vec3 max);
};

struct Skin {
  std::string            name;
  Node*                  skeleton_root = nullptr;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<Node*>     joints;
};

struct Node {
  Node*              parent;
  uint32_t           index;
  std::vector<Node*> children;
  glm::mat4          matrix;
  std::string        name;
  Mesh*              mesh;
  Skin*              skin;
  int32_t            skin_index = -1;
  glm::vec3          translation {};
  glm::vec3          scale {1.0f};
  glm::quat          rotation {};
  BoundingBox        bvh;
  BoundingBox        aabb;

  glm::mat4
  local_matrix();

  glm::mat4
  get_matrix();

  void
  update();

  ~Node();
};

struct AnimationChannel {
  enum PathType {
    TRANSLATION,
    ROTATION,
    SCALE
  };
  PathType path;
  Node*    node;
  uint32_t samplerIndex;
};

struct AnimationSampler {
  enum InterpolationType {
    LINEAR,
    STEP,
    CUBICSPLINE
  };
  InterpolationType      interpolation;
  std::vector<float>     inputs;
  std::vector<glm::vec4> outputs_vec4;
};

struct Animation {
  std::string                   name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float                         start = std::numeric_limits<float>::max();
  float                         end = std::numeric_limits<float>::min();
};

struct Model {
  ac_device device;

  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 color;
  };

  ac_buffer vertices;
  ac_buffer indices;
  ac_buffer matrices;

  glm::mat4 aabb;

  size_t mesh_count;

  std::vector<Node*> nodes;
  std::vector<Node*> linear_nodes;

  std::vector<Skin*> skins;

  std::vector<Texture>        textures;
  std::vector<TextureSampler> texture_samplers;
  std::vector<Material>       materials;
  std::vector<Animation>      animations;
  std::vector<std::string>    extensions;

  struct Dimensions {
    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);
  } dimensions;

  struct LoaderInfo {
    uint32_t* index_buffer;
    Vertex*   vertex_buffer;
    size_t    index_pos = 0;
    size_t    vertex_pos = 0;
  };

  void
  destroy(ac_device device);

  void
  load_node(
    Node*                  parent,
    const tinygltf::Node&  node,
    uint32_t               node_index,
    const tinygltf::Model& model,
    LoaderInfo&            loader_info,
    float                  globalscale);

  void
  get_node_props(
    const tinygltf::Node&  node,
    const tinygltf::Model& model,
    size_t&                vertex_count,
    size_t&                index_count);

  void
  load_skins(tinygltf::Model& model);

  void
  load_textures(tinygltf::Model& model, ac_device device, ac_queue copy_queue);

  ac_sampler_address_mode
  get_address_mode(int32_t wrap_mode);

  ac_filter
  get_ac_filter(int32_t filter);

  void
  load_texture_samplers(tinygltf::Model& model);

  void
  load_materials(tinygltf::Model& model);

  void
  load_animations(tinygltf::Model& model);

  ac_result
  load_from_file(
    const std::string& filename,
    ac_device          device,
    ac_queue           copy_queue,
    float              scale = 1.0f);

  void
  draw_node(Node* node, ac_cmd cmd);

  void
  draw(ac_cmd cmd);

  void
  calculate_bounding_box(Node* node, Node* parent);

  void
  get_scene_dimensions(void);

  void
  update_animation(uint32_t index, float time);

  Node*
  find_node(Node* parent, uint32_t index);

  Node*
  node_from_index(uint32_t index);
};
