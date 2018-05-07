#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include <android/asset_manager.h>

#include "VulkanUtil.h"
#include "ModelLoader.h"
#include "Buffers.h"

ModelLoader::ModelLoader(VkDevice device, android_app* app) :
    mDevice(device),
    androidAppCtx(app)
{
}

ModelLoader::~ModelLoader() {

}

void ModelLoader::LoadFromFile(const char* filePath, Model* model)
{
  const aiScene* scene;
  Assimp::Importer Importer;

  // Flags for loading the mesh
  static const int assimpFlags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices;

  assert(androidAppCtx);
  AAsset* file = AAssetManager_open(androidAppCtx->activity->assetManager, filePath, AASSET_MODE_STREAMING);

  size_t fileLength = AAsset_getLength(file);
  assert(fileLength > 0);
  char* meshData = new char[fileLength];

  AAsset_read(file, meshData, fileLength);

  AAsset_close(file);

  scene = Importer.ReadFileFromMemory(meshData, fileLength, assimpFlags);
  if (scene == nullptr) {
    LOGE("Importer Error: %s", Importer.GetErrorString());
  }
  assert(scene);

  delete[] meshData;

  model->parts.clear();
  model->parts.resize(scene->mNumMeshes);

  std::vector<float> vertexBuffer;
  std::vector<uint32_t> indexBuffer;

  model->vertexCount = 0;
  model->indexCount = 0;

  // Load meshes
  for (unsigned int i = 0; i < scene->mNumMeshes; i++)
  {
    const aiMesh* paiMesh = scene->mMeshes[i];

    model->parts[i] = {};
    model->parts[i].vertexBase = model->vertexCount;
    model->parts[i].indexBase = model->indexCount;

    model->vertexCount += scene->mMeshes[i]->mNumVertices;

    aiColor3D pColor(0.0f, 0.0f, 0.0f);
    scene->mMaterials[paiMesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, pColor);

    const aiVector3D Zero3D(0.0f, 0.0f, 0.0f);

    for (unsigned int j = 0; j < paiMesh->mNumVertices; j++)
    {
      const aiVector3D* pPos = &(paiMesh->mVertices[j]);
      const aiVector3D* pNormal = &(paiMesh->mNormals[j]);
      const aiVector3D* pTexCoord = (paiMesh->HasTextureCoords(0)) ? &(paiMesh->mTextureCoords[0][j]) : &Zero3D;
      const aiVector3D* pTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mTangents[j]) : &Zero3D;
      const aiVector3D* pBiTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mBitangents[j]) : &Zero3D;

      for (auto& component : model->layout.components)
      {
        switch (component) {
          case VERTEX_COMPONENT_POSITION:
            vertexBuffer.push_back(pPos->x * model->createInfo.scale.x + model->createInfo.center.x);
            vertexBuffer.push_back(-pPos->y * model->createInfo.scale.y + model->createInfo.center.y);
            vertexBuffer.push_back(pPos->z * model->createInfo.scale.z + model->createInfo.center.z);
            break;
          case VERTEX_COMPONENT_NORMAL:
            vertexBuffer.push_back(pNormal->x);
            vertexBuffer.push_back(-pNormal->y);
            vertexBuffer.push_back(pNormal->z);
            break;
          case VERTEX_COMPONENT_UV:
            vertexBuffer.push_back(pTexCoord->x * model->createInfo.uvscale.s);
            vertexBuffer.push_back(pTexCoord->y * model->createInfo.uvscale.t);
            break;
          case VERTEX_COMPONENT_COLOR:
            vertexBuffer.push_back(pColor.r);
            vertexBuffer.push_back(pColor.g);
            vertexBuffer.push_back(pColor.b);
            break;
          case VERTEX_COMPONENT_TANGENT:
            vertexBuffer.push_back(pTangent->x);
            vertexBuffer.push_back(pTangent->y);
            vertexBuffer.push_back(pTangent->z);
            break;
          case VERTEX_COMPONENT_BITANGENT:
            vertexBuffer.push_back(pBiTangent->x);
            vertexBuffer.push_back(pBiTangent->y);
            vertexBuffer.push_back(pBiTangent->z);
            break;
            // Dummy components for padding
          case VERTEX_COMPONENT_DUMMY_FLOAT:
            vertexBuffer.push_back(0.0f);
            break;
          case VERTEX_COMPONENT_DUMMY_VEC4:
            vertexBuffer.push_back(0.0f);
            vertexBuffer.push_back(0.0f);
            vertexBuffer.push_back(0.0f);
            vertexBuffer.push_back(0.0f);
            break;
        };
      }

    }

    model->parts[i].vertexCount = paiMesh->mNumVertices;

    uint32_t indexBase = static_cast<uint32_t>(indexBuffer.size());
    for (unsigned int j = 0; j < paiMesh->mNumFaces; j++)
    {
      const aiFace& Face = paiMesh->mFaces[j];
      if (Face.mNumIndices != 3)
        continue;
      indexBuffer.push_back(indexBase + Face.mIndices[0]);
      indexBuffer.push_back(indexBase + Face.mIndices[1]);
      indexBuffer.push_back(indexBase + Face.mIndices[2]);
      model->parts[i].indexCount += 3;
      model->indexCount += 3;
    }
  }

  uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(float);
  uint32_t indexBufferSize = static_cast<uint32_t>(indexBuffer.size()) * sizeof(uint32_t);

  // Vertex buffer
  CALL_VK(navs::CreateBuffer(
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      vertexBufferSize,
      &model->vertices.buffer,
      &model->vertices.memory,
      vertexBuffer.data()));

  // Index buffer
  CALL_VK(navs::CreateBuffer(
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      indexBufferSize,
      &model->indices.buffer,
      &model->indices.memory,
      indexBuffer.data()));
}