#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;

#include <openxr/openxr.h>
#include "d3d12.h"

#include "../import/tiny_gltf.h"

#include <vector>
#include <string>

struct EnvironmentVertex
{
    XrVector3f position;
    XrVector3f color;
    XrVector2f texCoord;
    //XrVector3f normal;
};

struct EnvironmentMesh
{
    std::vector<EnvironmentVertex> vertices;
    std::vector<uint16_t> indices;
};

template <typename T>
const T* ReadBuffer(tinygltf::Model& model, tinygltf::Accessor& accessor)
{
    tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    return reinterpret_cast<T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
}

bool LoadShadersFromFile(const wchar_t* shaderFileName, ComPtr<ID3DBlob>& vertexBytes, ComPtr<ID3DBlob>& pixelBytes, std::string& shaderError);

void LoadGltf(const char* path, EnvironmentMesh& targetMesh);