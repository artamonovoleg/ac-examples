#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT

#include "model.hpp"

BoundingBox::BoundingBox() {};

BoundingBox::BoundingBox(glm::vec3 min, glm::vec3 max)
    : min(min)
    , max(max) {};

BoundingBox
BoundingBox::get_aabb(glm::mat4 m)
{
  glm::vec3 min = glm::vec3(m[3]);
  glm::vec3 max = min;
  glm::vec3 v0, v1;

  glm::vec3 right = glm::vec3(m[0]);
  v0 = right * this->min.x;
  v1 = right * this->max.x;
  min += glm::min(v0, v1);
  max += glm::max(v0, v1);

  glm::vec3 up = glm::vec3(m[1]);
  v0 = up * this->min.y;
  v1 = up * this->max.y;
  min += glm::min(v0, v1);
  max += glm::max(v0, v1);

  glm::vec3 back = glm::vec3(m[2]);
  v0 = back * this->min.z;
  v1 = back * this->max.z;
  min += glm::min(v0, v1);
  max += glm::max(v0, v1);

  return BoundingBox(min, max);
}

void
Texture::destroy()
{
  ac_destroy_image(image);
}

ac_result
Texture::from_gltf_image(
  tinygltf::Image& gltfimage,
  TextureSampler   texture_sampler,
  ac_device        device,
  ac_queue         copy_queue)
{
  this->device = device;

  unsigned char* buffer = nullptr;
  uint64_t       buffer_size = 0;
  bool           deleteBuffer = false;
  if (gltfimage.component == 3)
  {
    buffer_size = gltfimage.width * gltfimage.height * 4;
    buffer = new unsigned char[buffer_size];
    unsigned char* rgba = buffer;
    unsigned char* rgb = &gltfimage.image[0];
    for (int32_t i = 0; i < gltfimage.width * gltfimage.height; ++i)
    {
      for (int32_t j = 0; j < 3; ++j)
      {
        rgba[j] = rgb[j];
      }
      rgba += 4;
      rgb += 3;
    }
    deleteBuffer = true;
  }
  else
  {
    buffer = &gltfimage.image[0];
    buffer_size = gltfimage.image.size();
  }

  ac_format format = ac_format_r8g8b8a8_unorm;

  uint32_t width = gltfimage.width;
  uint32_t height = gltfimage.height;
  uint32_t levels =
    static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

  // TODO: generate mips
  levels = 1;

  ac_buffer staging_buffer;

  ac_buffer_info buffer_info = {};
  buffer_info.size = buffer_size;
  buffer_info.usage = ac_buffer_usage_transfer_src_bit;
  buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;

  AC_RIF(ac_create_buffer(device, &buffer_info, &staging_buffer));

  AC_RIF(ac_buffer_map_memory(staging_buffer));
  memcpy(ac_buffer_get_mapped_memory(staging_buffer), buffer, buffer_size);
  ac_buffer_unmap_memory(staging_buffer);

  ac_image_info image_info = {};
  image_info.type = ac_image_type_2d;
  image_info.format = format;
  image_info.levels = levels;
  image_info.layers = 1;
  image_info.samples = 1;
  image_info.usage = ac_image_usage_srv_bit;
  image_info.width = width;
  image_info.height = height;
  image_info.usage = ac_image_usage_transfer_dst_bit |
                     ac_image_usage_transfer_src_bit | ac_image_usage_srv_bit;

  AC_RIF(ac_create_image(device, &image_info, &image));

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = copy_queue;
  ac_cmd_pool pool = NULL;
  AC_RIF(ac_create_cmd_pool(device, &pool_info, &pool));
  ac_cmd cmd = NULL;

  AC_RIF(ac_create_cmd(pool, &cmd));

  ac_begin_cmd(cmd);

  {
    ac_image_barrier barrier = {};
    barrier.old_layout = ac_image_layout_undefined;
    barrier.new_layout = ac_image_layout_transfer_dst;
    barrier.src_access = ac_access_none;
    barrier.dst_access = ac_access_transfer_write_bit;
    barrier.src_stage = ac_pipeline_stage_all_commands_bit;
    barrier.dst_stage = ac_pipeline_stage_all_commands_bit;
    barrier.image = image;

    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  ac_buffer_image_copy buffer_image_copy = {};
  buffer_image_copy.width = width;
  buffer_image_copy.height = height;

  ac_cmd_copy_buffer_to_image(cmd, staging_buffer, image, &buffer_image_copy);

  {
    ac_image_barrier barrier = {};
    barrier.old_layout = ac_image_layout_transfer_dst;
    barrier.new_layout = ac_image_layout_shader_read;
    barrier.src_access = ac_access_transfer_write_bit;
    barrier.dst_access = ac_access_shader_read_bit;
    barrier.src_stage = ac_pipeline_stage_all_commands_bit;
    barrier.dst_stage = ac_pipeline_stage_all_commands_bit;
    barrier.image = image;

    ac_cmd_barrier(cmd, 0, NULL, 1, &barrier);
  }

  ac_end_cmd(cmd);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;
  ac_queue_submit(copy_queue, &submit_info);

  ac_queue_wait_idle(copy_queue);

  ac_destroy_buffer(staging_buffer);

  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);

  if (deleteBuffer)
  {
    delete[] buffer;
  }

  return ac_result_success;
}

// Primitive
Primitive::Primitive(
  uint32_t  first_index,
  uint32_t  index_count,
  uint32_t  vertex_count,
  Material& material)
    : first_index(first_index)
    , index_count(index_count)
    , vertex_count(vertex_count)
    , material(material)
{
  has_indices = index_count > 0;
};

void
Primitive::set_bounding_box(glm::vec3 min, glm::vec3 max)
{
  bb.min = min;
  bb.max = max;
  bb.valid = true;
}

Mesh::Mesh(Model* m, glm::mat4 matrix)
{
  this->uniform_block = &(reinterpret_cast<Mesh::UniformBlock*>(
    ac_buffer_get_mapped_memory(m->matrices)))[m->mesh_count];
  this->uniform_block->matrix = matrix;
  this->node = m->mesh_count;
};

Mesh::~Mesh()
{
  for (Primitive* p : primitives)
  {
    delete p;
  }
}

void
Mesh::set_bounding_box(glm::vec3 min, glm::vec3 max)
{
  bb.min = min;
  bb.max = max;
  bb.valid = true;
}

glm::mat4
Node::local_matrix()
{
  return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) *
         glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4
Node::get_matrix()
{
  glm::mat4 m = local_matrix();
  Node*     p = parent;
  while (p)
  {
    m = p->local_matrix() * m;
    p = p->parent;
  }
  return m;
}

void
Node::update()
{
  if (mesh)
  {
    glm::mat4 m = get_matrix();

    if (skin)
    {
      mesh->uniform_block->matrix = m;
      glm::mat4 inverse_transform = glm::inverse(m);
      size_t    num_joints =
        std::min((uint32_t)skin->joints.size(), MAX_NUM_JOINTS);
      for (size_t i = 0; i < num_joints; i++)
      {
        Node*     joint_node = skin->joints[i];
        glm::mat4 joint_mat =
          joint_node->get_matrix() * skin->inverse_bind_matrices[i];
        joint_mat = inverse_transform * joint_mat;
        mesh->uniform_block->joint_matrix[i] = joint_mat;
      }
      mesh->uniform_block->joint_count = (float)num_joints;
    }
    else
    {
      memcpy(this->mesh->uniform_block, &m, sizeof(glm::mat4));
    }
  }

  for (auto& child : children)
  {
    child->update();
  }
}

Node::~Node()
{
  delete mesh;

  for (auto& child : children)
  {
    delete child;
  }
}

void
Model::destroy(ac_device device)
{
  ac_destroy_buffer(matrices);
  matrices = NULL;
  ac_destroy_buffer(vertices);
  vertices = NULL;
  ac_destroy_buffer(indices);
  indices = NULL;

  for (auto texture : textures)
  {
    texture.destroy();
  }
  textures.resize(0);
  texture_samplers.resize(0);
  for (auto node : nodes)
  {
    delete node;
  }
  materials.resize(0);
  animations.resize(0);
  nodes.resize(0);
  linear_nodes.resize(0);
  extensions.resize(0);
  for (auto skin : skins)
  {
    delete skin;
  }
  skins.resize(0);
};

void
Model::load_node(
  Node*                  parent,
  const tinygltf::Node&  node,
  uint32_t               nodeIndex,
  const tinygltf::Model& model,
  LoaderInfo&            loaderInfo,
  float                  globalscale)
{
  Node* newNode = new Node {};
  newNode->index = nodeIndex;
  newNode->parent = parent;
  newNode->name = node.name;
  newNode->skin_index = node.skin;
  newNode->matrix = glm::mat4(1.0f);

  glm::vec3 translation = glm::vec3(0.0f);
  if (node.translation.size() == 3)
  {
    translation = glm::make_vec3(node.translation.data());
    newNode->translation = translation;
  }
  // glm::mat4 rotation = glm::mat4( 1.0f );
  if (node.rotation.size() == 4)
  {
    glm::quat q = glm::make_quat(node.rotation.data());
    newNode->rotation = glm::mat4(q);
  }
  glm::vec3 scale = glm::vec3(1.0f);
  if (node.scale.size() == 3)
  {
    scale = glm::make_vec3(node.scale.data());
    newNode->scale = scale;
  }
  if (node.matrix.size() == 16)
  {
    newNode->matrix = glm::make_mat4x4(node.matrix.data());
  };

  // Node with children
  if (node.children.size() > 0)
  {
    for (size_t i = 0; i < node.children.size(); i++)
    {
      load_node(
        newNode,
        model.nodes[node.children[i]],
        node.children[i],
        model,
        loaderInfo,
        globalscale);
    }
  }

  if (node.mesh > -1)
  {
    const tinygltf::Mesh mesh = model.meshes[node.mesh];
    Mesh*                newMesh = new Mesh(this, newNode->matrix);
    this->mesh_count++;
    for (size_t j = 0; j < mesh.primitives.size(); j++)
    {
      const tinygltf::Primitive& primitive = mesh.primitives[j];
      uint32_t  vertexStart = static_cast<uint32_t>(loaderInfo.vertex_pos);
      uint32_t  indexStart = static_cast<uint32_t>(loaderInfo.index_pos);
      uint32_t  index_count = 0;
      uint32_t  vertex_count = 0;
      glm::vec3 posMin {};
      glm::vec3 posMax {};
      bool      hasSkin = false;
      bool      hasIndices = primitive.indices > -1;

      {
        const float* bufferPos = nullptr;
        const float* bufferNormals = nullptr;
        const float* bufferTexCoordSet0 = nullptr;
        const float* bufferTexCoordSet1 = nullptr;
        const float* bufferColorSet0 = nullptr;
        const void*  bufferJoints = nullptr;
        const float* bufferWeights = nullptr;

        int32_t posByteStride;
        int32_t normByteStride;
        int32_t uv0ByteStride;
        int32_t uv1ByteStride;
        int32_t color0ByteStride;
        int32_t jointByteStride;
        int32_t weightByteStride;

        int32_t jointComponentType;

        AC_ASSERT(
          primitive.attributes.find("POSITION") != primitive.attributes.end());

        const tinygltf::Accessor& posAccessor =
          model.accessors[primitive.attributes.find("POSITION")->second];
        const tinygltf::BufferView& posView =
          model.bufferViews[posAccessor.bufferView];
        bufferPos = reinterpret_cast<const float*>(
          &(model.buffers[posView.buffer]
              .data[posAccessor.byteOffset + posView.byteOffset]));
        posMin = glm::vec3(
          posAccessor.minValues[0],
          posAccessor.minValues[1],
          posAccessor.minValues[2]);
        posMax = glm::vec3(
          posAccessor.maxValues[0],
          posAccessor.maxValues[1],
          posAccessor.maxValues[2]);
        vertex_count = static_cast<uint32_t>(posAccessor.count);
        posByteStride =
          posAccessor.ByteStride(posView)
            ? (posAccessor.ByteStride(posView) / sizeof(float))
            : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

        if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
        {
          const tinygltf::Accessor& normAccessor =
            model.accessors[primitive.attributes.find("NORMAL")->second];
          const tinygltf::BufferView& normView =
            model.bufferViews[normAccessor.bufferView];
          bufferNormals = reinterpret_cast<const float*>(
            &(model.buffers[normView.buffer]
                .data[normAccessor.byteOffset + normView.byteOffset]));
          normByteStride =
            normAccessor.ByteStride(normView)
              ? (normAccessor.ByteStride(normView) / sizeof(float))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }

        // UVs
        if (
          primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
        {
          const tinygltf::Accessor& uvAccessor =
            model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
          const tinygltf::BufferView& uvView =
            model.bufferViews[uvAccessor.bufferView];
          bufferTexCoordSet0 = reinterpret_cast<const float*>(
            &(model.buffers[uvView.buffer]
                .data[uvAccessor.byteOffset + uvView.byteOffset]));
          uv0ByteStride =
            uvAccessor.ByteStride(uvView)
              ? (uvAccessor.ByteStride(uvView) / sizeof(float))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
        }
        if (
          primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end())
        {
          const tinygltf::Accessor& uvAccessor =
            model.accessors[primitive.attributes.find("TEXCOORD_1")->second];
          const tinygltf::BufferView& uvView =
            model.bufferViews[uvAccessor.bufferView];
          bufferTexCoordSet1 = reinterpret_cast<const float*>(
            &(model.buffers[uvView.buffer]
                .data[uvAccessor.byteOffset + uvView.byteOffset]));
          uv1ByteStride =
            uvAccessor.ByteStride(uvView)
              ? (uvAccessor.ByteStride(uvView) / sizeof(float))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
        }

        if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
        {
          const tinygltf::Accessor& accessor =
            model.accessors[primitive.attributes.find("COLOR_0")->second];
          const tinygltf::BufferView& view =
            model.bufferViews[accessor.bufferView];
          bufferColorSet0 = reinterpret_cast<const float*>(
            &(model.buffers[view.buffer]
                .data[accessor.byteOffset + view.byteOffset]));
          color0ByteStride =
            accessor.ByteStride(view)
              ? (int32_t)(accessor.ByteStride(view) / sizeof(float))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }

        if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end())
        {
          const tinygltf::Accessor& jointAccessor =
            model.accessors[primitive.attributes.find("JOINTS_0")->second];
          const tinygltf::BufferView& jointView =
            model.bufferViews[jointAccessor.bufferView];
          bufferJoints =
            &(model.buffers[jointView.buffer]
                .data[jointAccessor.byteOffset + jointView.byteOffset]);
          jointComponentType = jointAccessor.componentType;
          jointByteStride =
            jointAccessor.ByteStride(jointView)
              ? (jointAccessor.ByteStride(jointView) /
                 tinygltf::GetComponentSizeInBytes(jointComponentType))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }

        if (
          primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end())
        {
          const tinygltf::Accessor& weightAccessor =
            model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
          const tinygltf::BufferView& weightView =
            model.bufferViews[weightAccessor.bufferView];
          bufferWeights = reinterpret_cast<const float*>(
            &(model.buffers[weightView.buffer]
                .data[weightAccessor.byteOffset + weightView.byteOffset]));
          weightByteStride =
            weightAccessor.ByteStride(weightView)
              ? (int32_t)(weightAccessor.ByteStride(weightView) / sizeof(float))
              : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }

        hasSkin = (bufferJoints && bufferWeights);

        for (size_t v = 0; v < posAccessor.count; v++)
        {
          Vertex& vert = loaderInfo.vertex_buffer[loaderInfo.vertex_pos];
          vert.pos =
            glm::vec4(glm::make_vec3(&bufferPos[v * posByteStride]), 1.0f);
          vert.normal = glm::normalize(glm::vec3(
            bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride])
                          : glm::vec3(0.0f)));
          vert.uv0 = bufferTexCoordSet0
                       ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride])
                       : glm::vec3(0.0f);
          vert.uv1 = bufferTexCoordSet1
                       ? glm::make_vec2(&bufferTexCoordSet1[v * uv1ByteStride])
                       : glm::vec3(0.0f);
          vert.color =
            bufferColorSet0
              ? glm::make_vec4(&bufferColorSet0[v * color0ByteStride])
              : glm::vec4(1.0f);

          if (hasSkin)
          {
            switch (jointComponentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
              const uint16_t* buf = static_cast<const uint16_t*>(bufferJoints);
              vert.joint0 =
                glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
              break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
              const uint8_t* buf = static_cast<const uint8_t*>(bufferJoints);
              vert.joint0 =
                glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
              break;
            }
            default:
            {
              std::cerr << "Joint component type " << jointComponentType
                        << " not supported!" << std::endl;
              break;
            }
            }
          }
          else
          {
            vert.joint0 = glm::vec4(0.0f);
          }
          vert.weight0 =
            hasSkin ? glm::make_vec4(&bufferWeights[v * weightByteStride])
                    : glm::vec4(0.0f);
          // Fix for all zero weights
          if (glm::length(vert.weight0) == 0.0f)
          {
            vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
          }
          loaderInfo.vertex_pos++;
        }
      }
      // Indices
      if (hasIndices)
      {
        const tinygltf::Accessor& accessor =
          model.accessors[primitive.indices > -1 ? primitive.indices : 0];
        const tinygltf::BufferView& bufferView =
          model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

        index_count = static_cast<uint32_t>(accessor.count);
        const void* dataPtr =
          &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

        switch (accessor.componentType)
        {
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
        {
          const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
          for (size_t index = 0; index < accessor.count; index++)
          {
            loaderInfo.index_buffer[loaderInfo.index_pos] =
              buf[index] + vertexStart;
            loaderInfo.index_pos++;
          }
          break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
        {
          const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
          for (size_t index = 0; index < accessor.count; index++)
          {
            loaderInfo.index_buffer[loaderInfo.index_pos] =
              buf[index] + vertexStart;
            loaderInfo.index_pos++;
          }
          break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
        {
          const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
          for (size_t index = 0; index < accessor.count; index++)
          {
            loaderInfo.index_buffer[loaderInfo.index_pos] =
              buf[index] + vertexStart;
            loaderInfo.index_pos++;
          }
          break;
        }
        default:
          std::cerr << "Index component type " << accessor.componentType
                    << " not supported!" << std::endl;
          return;
        }
      }
      Primitive* newPrimitive = new Primitive(
        indexStart,
        index_count,
        vertex_count,
        primitive.material > -1 ? materials[primitive.material]
                                : materials.back());
      newPrimitive->set_bounding_box(posMin, posMax);
      newMesh->primitives.push_back(newPrimitive);
    }
    // Mesh BB from BBs of primitives
    for (auto p : newMesh->primitives)
    {
      if (p->bb.valid && !newMesh->bb.valid)
      {
        newMesh->bb = p->bb;
        newMesh->bb.valid = true;
      }
      newMesh->bb.min = glm::min(newMesh->bb.min, p->bb.min);
      newMesh->bb.max = glm::max(newMesh->bb.max, p->bb.max);
    }
    newNode->mesh = newMesh;
  }
  if (parent)
  {
    parent->children.push_back(newNode);
  }
  else
  {
    nodes.push_back(newNode);
  }
  linear_nodes.push_back(newNode);
}

void
Model::get_node_props(
  const tinygltf::Node&  node,
  const tinygltf::Model& model,
  size_t&                vertex_count,
  size_t&                index_count)
{
  if (node.children.size() > 0)
  {
    for (size_t i = 0; i < node.children.size(); i++)
    {
      get_node_props(
        model.nodes[node.children[i]],
        model,
        vertex_count,
        index_count);
    }
  }
  if (node.mesh > -1)
  {
    const tinygltf::Mesh mesh = model.meshes[node.mesh];
    for (size_t i = 0; i < mesh.primitives.size(); i++)
    {
      auto primitive = mesh.primitives[i];
      vertex_count +=
        model.accessors[primitive.attributes.find("POSITION")->second].count;
      if (primitive.indices > -1)
      {
        index_count += model.accessors[primitive.indices].count;
      }
    }
  }
}

void
Model::load_skins(tinygltf::Model& gltf_model)
{
  for (tinygltf::Skin& source : gltf_model.skins)
  {
    Skin* newSkin = new Skin {};
    newSkin->name = source.name;

    // Find skeleton root node
    if (source.skeleton > -1)
    {
      newSkin->skeleton_root = node_from_index(source.skeleton);
    }

    // Find joint nodes
    for (int jointIndex : source.joints)
    {
      Node* node = node_from_index(jointIndex);
      if (node)
      {
        newSkin->joints.push_back(node_from_index(jointIndex));
      }
    }

    // Get inverse bind matrices from buffer
    if (source.inverseBindMatrices > -1)
    {
      const tinygltf::Accessor& accessor =
        gltf_model.accessors[source.inverseBindMatrices];
      const tinygltf::BufferView& bufferView =
        gltf_model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer& buffer = gltf_model.buffers[bufferView.buffer];
      newSkin->inverse_bind_matrices.resize(accessor.count);
      memcpy(
        newSkin->inverse_bind_matrices.data(),
        &buffer.data[accessor.byteOffset + bufferView.byteOffset],
        accessor.count * sizeof(glm::mat4));
    }

    skins.push_back(newSkin);
  }
}

void
Model::load_textures(
  tinygltf::Model& gltf_model,
  ac_device        device,
  ac_queue         transfer_queue)
{
  for (tinygltf::Texture& tex : gltf_model.textures)
  {
    tinygltf::Image image = gltf_model.images[tex.source];
    TextureSampler  textureSampler;
    if (tex.sampler == -1)
    {
      textureSampler.mag_filter = ac_filter_linear;
      textureSampler.min_filter = ac_filter_linear;
      textureSampler.address_mode_u = ac_sampler_address_mode_repeat;
      textureSampler.address_mode_v = ac_sampler_address_mode_repeat;
      textureSampler.address_mode_w = ac_sampler_address_mode_repeat;
    }
    else
    {
      textureSampler = texture_samplers[tex.sampler];
    }
    Texture texture;
    (void)
      texture.from_gltf_image(image, textureSampler, device, transfer_queue);
    textures.push_back(texture);
  }
}

ac_sampler_address_mode
Model::get_address_mode(int32_t wrap_mode)
{
  switch (wrap_mode)
  {
  case -1:
  case 10497:
    return ac_sampler_address_mode_repeat;
  case 33071:
    return ac_sampler_address_mode_clamp_to_edge;
  case 33648:
    return ac_sampler_address_mode_mirrored_repeat;
  }

  std::cerr << "Unknown wrap mode for get_address_mode: " << wrap_mode
            << std::endl;
  return ac_sampler_address_mode_repeat;
}

ac_filter
Model::get_ac_filter(int32_t filter)
{
  switch (filter)
  {
  case -1:
  case 9728:
    return ac_filter_nearest;
  case 9729:
    return ac_filter_linear;
  case 9984:
    return ac_filter_nearest;
  case 9985:
    return ac_filter_nearest;
  case 9986:
    return ac_filter_linear;
  case 9987:
    return ac_filter_linear;
  }

  std::cerr << "Unknown filter mode for get_ac_filter: " << filter << std::endl;
  return ac_filter_nearest;
}

void
Model::load_texture_samplers(tinygltf::Model& model)
{
  for (tinygltf::Sampler smpl : model.samplers)
  {
    TextureSampler sampler {};
    sampler.min_filter = get_ac_filter(smpl.minFilter);
    sampler.mag_filter = get_ac_filter(smpl.magFilter);
    sampler.address_mode_u = get_address_mode(smpl.wrapS);
    sampler.address_mode_v = get_address_mode(smpl.wrapT);
    sampler.address_mode_w = sampler.address_mode_v;
    texture_samplers.push_back(sampler);
  }
}

void
Model::load_materials(tinygltf::Model& model)
{
  for (tinygltf::Material& mat : model.materials)
  {
    Material material {};
    material.double_sided = mat.doubleSided;
    if (mat.values.find("baseColorTexture") != mat.values.end())
    {
      material.base_color_texture =
        &textures[mat.values["baseColorTexture"].TextureIndex()];
      material.tex_coord_sets.base_color =
        mat.values["baseColorTexture"].TextureIndex();
    }
    if (mat.values.find("metallicRoughnessTexture") != mat.values.end())
    {
      material.metallic_roughness_texture =
        &textures[mat.values["metallicRoughnessTexture"].TextureIndex()];
      material.tex_coord_sets.metallic_roughness =
        mat.values["metallicRoughnessTexture"].TextureIndex();
    }
    if (mat.values.find("roughnessFactor") != mat.values.end())
    {
      material.roughness_factor =
        static_cast<float>(mat.values["roughnessFactor"].Factor());
    }
    if (mat.values.find("metallicFactor") != mat.values.end())
    {
      material.metallic_factor =
        static_cast<float>(mat.values["metallicFactor"].Factor());
    }
    if (mat.values.find("baseColorFactor") != mat.values.end())
    {
      material.base_color_factor =
        glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
    }
    if (
      mat.additionalValues.find("normalTexture") != mat.additionalValues.end())
    {
      material.normal_texture =
        &textures[mat.additionalValues["normalTexture"].TextureIndex()];
      material.tex_coord_sets.normal =
        mat.additionalValues["normalTexture"].TextureIndex();
    }
    if (
      mat.additionalValues.find("emissiveTexture") !=
      mat.additionalValues.end())
    {
      material.emissive_texture =
        &textures[mat.additionalValues["emissiveTexture"].TextureIndex()];
      material.tex_coord_sets.emissive =
        mat.additionalValues["emissiveTexture"].TextureIndex();
    }
    if (
      mat.additionalValues.find("occlusionTexture") !=
      mat.additionalValues.end())
    {
      material.occlusion_texture =
        &textures[mat.additionalValues["occlusionTexture"].TextureIndex()];
      material.tex_coord_sets.occlusion =
        mat.additionalValues["occlusionTexture"].TextureIndex();
    }
    if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end())
    {
      tinygltf::Parameter param = mat.additionalValues["alphaMode"];
      if (param.string_value == "BLEND")
      {
        material.alpha_mode = Material::ALPHAMODE_BLEND;
      }
      if (param.string_value == "MASK")
      {
        material.alpha_cutoff = 0.5f;
        material.alpha_mode = Material::ALPHAMODE_MASK;
      }
    }
    if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end())
    {
      material.alpha_cutoff =
        static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
    }
    if (
      mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end())
    {
      material.emissive_factor = glm::vec4(
        glm::make_vec3(
          mat.additionalValues["emissiveFactor"].ColorFactor().data()),
        1.0);
    }

    if (
      mat.extensions.find("KHR_materials_emissive_strength") !=
      mat.extensions.end())
    {
      auto ext = mat.extensions.find("KHR_materials_emissive_strength");
      if (ext->second.Has("emissiveStrength"))
      {
        auto value = ext->second.Get("emissiveStrength");
        material.emissive_strength = (float)value.Get<double>();
      }
    }

    if (
      mat.extensions.find("KHR_materials_pbrSpecularGlossiness") !=
      mat.extensions.end())
    {
      auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
      if (ext->second.Has("specularGlossinessTexture"))
      {
        auto index = ext->second.Get("specularGlossinessTexture").Get("index");
        material.extension.specular_glossiness_texture =
          &textures[index.Get<int>()];
        auto texCoordSet =
          ext->second.Get("specularGlossinessTexture").Get("texCoord");
        material.tex_coord_sets.specular_glossiness = texCoordSet.Get<int>();
        material.pbr_workflows.specular_glossiness = true;
      }
      if (ext->second.Has("diffuseTexture"))
      {
        auto index = ext->second.Get("diffuseTexture").Get("index");
        material.extension.diffuse_texture = &textures[index.Get<int>()];
      }
      if (ext->second.Has("diffuseFactor"))
      {
        auto factor = ext->second.Get("diffuseFactor");
        for (uint32_t i = 0; i < factor.ArrayLen(); i++)
        {
          auto val = factor.Get(i);
          material.extension.diffuse_factor[i] = val.IsNumber()
                                                   ? (float)val.Get<double>()
                                                   : (float)val.Get<int32_t>();
        }
      }
      if (ext->second.Has("specularFactor"))
      {
        auto factor = ext->second.Get("specularFactor");
        for (uint32_t i = 0; i < factor.ArrayLen(); i++)
        {
          auto val = factor.Get(i);
          material.extension.specular_factor[i] =
            val.IsNumber() ? static_cast<float>(val.Get<double>())
                           : static_cast<float>(val.Get<int32_t>());
        }
      }
    }

    material.index = static_cast<uint32_t>(materials.size());
    materials.push_back(material);
  }
  // Push a default material at the end of the list for meshes with no material
  // assigned
  materials.push_back(Material());
}

void
Model::load_animations(tinygltf::Model& gltf_model)
{
  for (tinygltf::Animation& anim : gltf_model.animations)
  {
    Animation animation {};
    animation.name = anim.name;
    if (anim.name.empty())
    {
      animation.name = std::to_string(animations.size());
    }

    // Samplers
    for (auto& samp : anim.samplers)
    {
      AnimationSampler sampler {};

      if (samp.interpolation == "LINEAR")
      {
        sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
      }
      if (samp.interpolation == "STEP")
      {
        sampler.interpolation = AnimationSampler::InterpolationType::STEP;
      }
      if (samp.interpolation == "CUBICSPLINE")
      {
        sampler.interpolation =
          AnimationSampler::InterpolationType::CUBICSPLINE;
      }

      // Read sampler input time values
      {
        const tinygltf::Accessor&   accessor = gltf_model.accessors[samp.input];
        const tinygltf::BufferView& bufferView =
          gltf_model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltf_model.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* dataPtr =
          &buffer.data[accessor.byteOffset + bufferView.byteOffset];
        const float* buf = static_cast<const float*>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++)
        {
          sampler.inputs.push_back(buf[index]);
        }

        for (auto input : sampler.inputs)
        {
          if (input < animation.start)
          {
            animation.start = input;
          };
          if (input > animation.end)
          {
            animation.end = input;
          }
        }
      }

      // Read sampler output T/R/S values
      {
        const tinygltf::Accessor& accessor = gltf_model.accessors[samp.output];
        const tinygltf::BufferView& bufferView =
          gltf_model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltf_model.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* dataPtr =
          &buffer.data[accessor.byteOffset + bufferView.byteOffset];

        switch (accessor.type)
        {
        case TINYGLTF_TYPE_VEC3:
        {
          const glm::vec3* buf = static_cast<const glm::vec3*>(dataPtr);
          for (size_t index = 0; index < accessor.count; index++)
          {
            sampler.outputs_vec4.push_back(glm::vec4(buf[index], 0.0f));
          }
          break;
        }
        case TINYGLTF_TYPE_VEC4:
        {
          const glm::vec4* buf = static_cast<const glm::vec4*>(dataPtr);
          for (size_t index = 0; index < accessor.count; index++)
          {
            sampler.outputs_vec4.push_back(buf[index]);
          }
          break;
        }
        default:
        {
          std::cout << "unknown type" << std::endl;
          break;
        }
        }
      }

      animation.samplers.push_back(sampler);
    }

    // Channels
    for (auto& source : anim.channels)
    {
      AnimationChannel channel {};

      if (source.target_path == "rotation")
      {
        channel.path = AnimationChannel::PathType::ROTATION;
      }
      if (source.target_path == "translation")
      {
        channel.path = AnimationChannel::PathType::TRANSLATION;
      }
      if (source.target_path == "scale")
      {
        channel.path = AnimationChannel::PathType::SCALE;
      }
      if (source.target_path == "weights")
      {
        std::cout << "weights not yet supported, skipping channel" << std::endl;
        continue;
      }
      channel.samplerIndex = source.sampler;
      channel.node = node_from_index(source.target_node);
      if (!channel.node)
      {
        continue;
      }

      animation.channels.push_back(channel);
    }

    animations.push_back(animation);
  }
}

ac_result
Model::load_from_file(
  const std::string& filename,
  ac_device          device,
  ac_queue           transfer_queue,
  float              scale)
{
  void*  mem = NULL;
  size_t length = 0;

  {
    ac_file file;
    AC_RIF(ac_create_file(
      AC_SYSTEM_FS,
      ac_mount_rom,
      filename.c_str(),
      ac_file_mode_read_bit,
      &file));

    length = ac_file_get_size(file);
    mem = ac_alloc(length);
    AC_RIF(ac_file_read(file, length, mem));

    ac_destroy_file(file);
  }

  tinygltf::Model    gltf_model;
  tinygltf::TinyGLTF gltf_context;

  tinygltf::FsCallbacks fs_callbacks;

  fs_callbacks.FileExists = [](const std::string& abs_filename, void*) -> bool
  {
    return ac_path_exists(AC_SYSTEM_FS, ac_mount_rom, abs_filename.c_str());
  };

  fs_callbacks.ExpandFilePath =
    [](const std::string& filepath, void*) -> std::string
  {
    return filepath;
  };
  fs_callbacks.ReadWholeFile = [](
                                 std::vector<unsigned char>* out,
                                 std::string*                err,
                                 const std::string&          path,
                                 void*) -> bool
  {
    ac_file file;
    if (
      ac_create_file(
        AC_SYSTEM_FS,
        ac_mount_rom,
        path.c_str(),
        ac_file_mode_read_bit,
        &file) != ac_result_success)
    {
      return false;
    }

    out->resize(ac_file_get_size(file));
    ac_file_read(file, ac_file_get_size(file), out->data());

    ac_destroy_file(file);

    return true;
  };

  fs_callbacks.GetFileSizeInBytes = [](
                                      size_t*            filesize_out,
                                      std::string*       err,
                                      const std::string& filepath,
                                      void*) -> bool
  {
    ac_file file;
    if (
      ac_create_file(
        AC_SYSTEM_FS,
        ac_mount_rom,
        filepath.c_str(),
        ac_file_mode_read_bit,
        &file) != ac_result_success)
    {
      return false;
    }

    *filesize_out = ac_file_get_size(file);

    ac_destroy_file(file);

    return true;
  };

  gltf_context.SetFsCallbacks(fs_callbacks);

  std::string error;
  std::string warning;

  this->device = device;

  bool   binary = false;
  size_t extpos = filename.rfind('.', filename.length());
  if (extpos != std::string::npos)
  {
    binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");
  }

  bool file_loaded = binary ? gltf_context.LoadBinaryFromMemory(
                                &gltf_model,
                                &error,
                                &warning,
                                (const unsigned char*)mem,
                                static_cast<uint32_t>(length))
                            : gltf_context.LoadASCIIFromString(
                                &gltf_model,
                                &error,
                                &warning,
                                (const char*)mem,
                                static_cast<uint32_t>(length),
                                "");

  LoaderInfo loaderInfo {};
  size_t     vertex_count = 0;
  size_t     index_count = 0;

  if (file_loaded)
  {
    load_texture_samplers(gltf_model);
    load_textures(gltf_model, device, transfer_queue);
    load_materials(gltf_model);

    const tinygltf::Scene& scene =
      gltf_model
        .scenes[gltf_model.defaultScene > -1 ? gltf_model.defaultScene : 0];

    // Get vertex and index buffer sizes up-front
    for (size_t i = 0; i < scene.nodes.size(); i++)
    {
      get_node_props(
        gltf_model.nodes[scene.nodes[i]],
        gltf_model,
        vertex_count,
        index_count);
    }
    loaderInfo.vertex_buffer = new Vertex[vertex_count];
    loaderInfo.index_buffer = new uint32_t[index_count];

    if (gltf_model.meshes.size())
    {
      ac_buffer_info buffer_info = {};
      buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
      buffer_info.usage = ac_buffer_usage_srv_bit;
      buffer_info.name = "matrix buffer";
      buffer_info.size = gltf_model.meshes.size() * sizeof(Mesh::UniformBlock);

      AC_RIF(ac_create_buffer(this->device, &buffer_info, &this->matrices));
      AC_RIF(ac_buffer_map_memory(this->matrices));
    }

    for (size_t i = 0; i < scene.nodes.size(); i++)
    {
      const tinygltf::Node node = gltf_model.nodes[scene.nodes[i]];
      load_node(nullptr, node, scene.nodes[i], gltf_model, loaderInfo, scale);
    }
    if (gltf_model.animations.size() > 0)
    {
      load_animations(gltf_model);
    }
    load_skins(gltf_model);

    for (auto node : linear_nodes)
    {
      // Assign skins
      if (node->skin_index > -1)
      {
        node->skin = skins[node->skin_index];
      }
      // Initial pose
      if (node->mesh)
      {
        node->update();
      }
    }
  }
  else
  {
    std::cerr << "Could not load gltf file: " << error << std::endl;
    return ac_result_unknown_error;
  }

  extensions = gltf_model.extensionsUsed;

  size_t vertex_buffer_size = vertex_count * sizeof(Vertex);
  size_t index_buffer_size = index_count * sizeof(uint32_t);

  AC_ASSERT(vertex_buffer_size > 0);

  ac_buffer vertex_staging = NULL;
  ac_buffer index_staging = NULL;

  {
    ac_buffer_info buffer_info = {};
    buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
    buffer_info.usage = ac_buffer_usage_transfer_src_bit;
    buffer_info.size = vertex_buffer_size;
    AC_RIF(ac_create_buffer(device, &buffer_info, &vertex_staging));
    AC_RIF(ac_buffer_map_memory(vertex_staging));
    memcpy(
      ac_buffer_get_mapped_memory(vertex_staging),
      loaderInfo.vertex_buffer,
      vertex_buffer_size);
  }

  if (index_buffer_size > 0)
  {
    ac_buffer_info buffer_info = {};
    buffer_info.memory_usage = ac_memory_usage_cpu_to_gpu;
    buffer_info.usage = ac_buffer_usage_transfer_src_bit;
    buffer_info.size = index_buffer_size;
    AC_RIF(ac_create_buffer(device, &buffer_info, &index_staging));
    AC_RIF(ac_buffer_map_memory(index_staging));

    memcpy(
      ac_buffer_get_mapped_memory(index_staging),
      loaderInfo.index_buffer,
      index_buffer_size);
  }

  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_gpu_only;
    info.usage = ac_buffer_usage_vertex_bit | ac_buffer_usage_transfer_dst_bit;
    info.size = vertex_buffer_size;
    AC_RIF(ac_create_buffer(device, &info, &vertices));
  }

  if (index_buffer_size > 0)
  {
    ac_buffer_info info = {};
    info.memory_usage = ac_memory_usage_gpu_only;
    info.usage = ac_buffer_usage_index_bit | ac_buffer_usage_transfer_dst_bit;
    info.size = index_buffer_size;
    AC_RIF(ac_create_buffer(device, &info, &indices));
  }

  ac_cmd_pool_info pool_info = {};
  pool_info.queue = transfer_queue;
  ac_cmd_pool pool;
  AC_RIF(ac_create_cmd_pool(device, &pool_info, &pool));

  ac_cmd cmd = NULL;
  AC_RIF(ac_create_cmd(pool, &cmd));

  ac_begin_cmd(cmd);

  ac_cmd_copy_buffer(cmd, vertex_staging, 0, vertices, 0, vertex_buffer_size);

  if (index_buffer_size > 0)
  {
    ac_cmd_copy_buffer(cmd, index_staging, 0, indices, 0, index_buffer_size);
  }

  ac_end_cmd(cmd);

  ac_queue_submit_info submit_info = {};
  submit_info.cmd_count = 1;
  submit_info.cmds = &cmd;

  AC_RIF(ac_queue_submit(transfer_queue, &submit_info));

  ac_queue_wait_idle(transfer_queue);

  ac_destroy_buffer(vertex_staging);
  ac_destroy_buffer(index_staging);

  ac_destroy_cmd(cmd);
  ac_destroy_cmd_pool(pool);

  delete[] loaderInfo.vertex_buffer;
  delete[] loaderInfo.index_buffer;

  get_scene_dimensions();

  ac_free(mem);

  return ac_result_success;
}

void
Model::draw_node(Node* node, ac_cmd cmd)
{
  if (node->mesh)
  {
    for (Primitive* primitive : node->mesh->primitives)
    {
      ac_cmd_draw_indexed(
        cmd,
        primitive->index_count,
        1,
        primitive->first_index,
        0,
        0);
    }
  }

  for (auto& child : node->children)
  {
    draw_node(child, cmd);
  }
}

void
Model::draw(ac_cmd cmd)
{
  ac_cmd_bind_vertex_buffer(cmd, 0, vertices, 0);
  ac_cmd_bind_index_buffer(cmd, indices, 0, ac_index_type_u32);
  for (auto& node : nodes)
  {
    draw_node(node, cmd);
  }
}

void
Model::calculate_bounding_box(Node* node, Node* parent)
{
  BoundingBox parent_bvh =
    parent ? parent->bvh : BoundingBox(dimensions.min, dimensions.max);

  if (node->mesh)
  {
    if (node->mesh->bb.valid)
    {
      node->aabb = node->mesh->bb.get_aabb(node->get_matrix());
      if (node->children.size() == 0)
      {
        node->bvh.min = node->aabb.min;
        node->bvh.max = node->aabb.max;
        node->bvh.valid = true;
      }
    }
  }

  parent_bvh.min = glm::min(parent_bvh.min, node->bvh.min);
  parent_bvh.max = glm::min(parent_bvh.max, node->bvh.max);

  for (auto& child : node->children)
  {
    calculate_bounding_box(child, node);
  }
}

void
Model::get_scene_dimensions()
{
  // Calculate binary volume hierarchy for all nodes in the scene
  for (auto node : linear_nodes)
  {
    calculate_bounding_box(node, nullptr);
  }

  dimensions.min = glm::vec3(FLT_MAX);
  dimensions.max = glm::vec3(-FLT_MAX);

  for (auto node : linear_nodes)
  {
    if (node->bvh.valid)
    {
      dimensions.min = glm::min(dimensions.min, node->bvh.min);
      dimensions.max = glm::max(dimensions.max, node->bvh.max);
    }
  }

  // Calculate scene aabb
  aabb = glm::scale(
    glm::mat4(1.0f),
    glm::vec3(
      dimensions.max[0] - dimensions.min[0],
      dimensions.max[1] - dimensions.min[1],
      dimensions.max[2] - dimensions.min[2]));
  aabb[3][0] = dimensions.min[0];
  aabb[3][1] = dimensions.min[1];
  aabb[3][2] = dimensions.min[2];
}

void
Model::update_animation(uint32_t index, float time)
{
  if (animations.empty())
  {
    std::cout << ".glTF does not contain animation." << std::endl;
    return;
  }
  if (index > static_cast<uint32_t>(animations.size()) - 1)
  {
    std::cout << "No animation with index " << index << std::endl;
    return;
  }
  Animation& animation = animations[index];

  bool updated = false;
  for (auto& channel : animation.channels)
  {
    AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
    if (sampler.inputs.size() > sampler.outputs_vec4.size())
    {
      continue;
    }

    for (size_t i = 0; i < sampler.inputs.size() - 1; i++)
    {
      if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1]))
      {
        float u = std::max(0.0f, time - sampler.inputs[i]) /
                  (sampler.inputs[i + 1] - sampler.inputs[i]);

        if (u <= 1.0f)
        {
          switch (channel.path)
          {
          case AnimationChannel::PathType::TRANSLATION:
          {
            glm::vec4 trans =
              glm::mix(sampler.outputs_vec4[i], sampler.outputs_vec4[i + 1], u);
            channel.node->translation = glm::vec3(trans);
            break;
          }
          case AnimationChannel::PathType::SCALE:
          {
            glm::vec4 trans =
              glm::mix(sampler.outputs_vec4[i], sampler.outputs_vec4[i + 1], u);
            channel.node->scale = glm::vec3(trans);
            break;
          }
          case AnimationChannel::PathType::ROTATION:
          {
            glm::quat q1;
            q1.x = sampler.outputs_vec4[i].x;
            q1.y = sampler.outputs_vec4[i].y;
            q1.z = sampler.outputs_vec4[i].z;
            q1.w = sampler.outputs_vec4[i].w;
            glm::quat q2;
            q2.x = sampler.outputs_vec4[i + 1].x;
            q2.y = sampler.outputs_vec4[i + 1].y;
            q2.z = sampler.outputs_vec4[i + 1].z;
            q2.w = sampler.outputs_vec4[i + 1].w;
            channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
            break;
          }
          }
          updated = true;
        }
      }
    }
  }

  if (updated)
  {
    for (auto& node : nodes)
    {
      node->update();
    }
  }
}

Node*
Model::find_node(Node* parent, uint32_t index)
{
  Node* node_found = nullptr;

  if (parent->index == index)
  {
    return parent;
  }

  for (auto& child : parent->children)
  {
    node_found = find_node(child, index);
    if (node_found)
    {
      break;
    }
  }

  return node_found;
}

Node*
Model::node_from_index(uint32_t index)
{
  Node* node_found = nullptr;

  for (auto& node : nodes)
  {
    node_found = find_node(node, index);
    if (node_found)
    {
      break;
    }
  }

  return node_found;
}
