#include "models.h"

#include "d3dcompiler.h"
#include <DirectXMath.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#include "../import/tiny_gltf.h"

using namespace tinygltf;
using namespace DirectX;

bool LoadShadersFromFile(const wchar_t* shaderFileName, ComPtr<ID3DBlob>& vertexBytes, ComPtr<ID3DBlob>& pixelBytes, std::string& shaderError)
{
    OutputDebugString(L"Loading shaders...\n");

    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    std::wstring path = std::wstring(L"../../../../OpenXRViewer/shaders/");
    path.append(shaderFileName);
    const wchar_t* shaderPath = path.c_str();
    Sleep(100);

    time_t startTime = time(nullptr);
    bool canOpen = false;
    while (!canOpen)
    {
        std::ifstream fileStream(shaderPath, std::ios::in);

        canOpen = fileStream.good();
        fileStream.close();

        if (!canOpen)
        {
            time_t elapsedSeconds = time(nullptr) - startTime;
            if (elapsedSeconds > 5)
            {
                throw std::format(L"Could not open shader file {}", shaderPath).c_str();
            }
            Sleep(100);
        }
    }

    ComPtr<ID3DBlob> vsErrors;
    ComPtr<ID3D10Blob> psErrors;

    shaderError.clear();
    HRESULT hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "MainVS", "vs_5_1", compileFlags, 0, &vertexBytes, &vsErrors);

    if (vsErrors)
    {
        shaderError = std::format("Vertex Shader Errors:\n{}\n", (LPCSTR)vsErrors->GetBufferPointer());
        OutputDebugString(std::format(L"{}\n", shaderPath).c_str());
        OutputDebugStringA(shaderError.c_str());
    }
    if (FAILED(hr)) return false;

    hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "MainPS", "ps_5_1", compileFlags, 0, &pixelBytes, &psErrors);
    if (psErrors)
    {
        shaderError = std::format("Pixel Shader Errors:\n{}\n", (LPCSTR)psErrors->GetBufferPointer());
        OutputDebugString(std::format(L"{}\n", shaderPath).c_str());
        OutputDebugStringA(shaderError.c_str());
    }
    if (FAILED(hr)) return false;

    return true;
}

void LoadGltf(const char* path, EnvironmentMesh& targetMesh)
{
    Model model;
    TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

    if (!warn.empty())
    {
        OutputDebugStringA(std::format("Warn: {}\n", warn).c_str());
    }

    if (!err.empty())
    {
        OutputDebugStringA(std::format("Err: {}\n", err).c_str());
    }

    if (!ret)
    {
        OutputDebugStringA(std::format("Failed to parse glTF\n").c_str());
    }

    Primitive& prim = model.meshes[0].primitives[0];
    Accessor& indices = model.accessors[prim.indices];
    Accessor& positions = model.accessors[prim.attributes["POSITION"]];
    //Accessor& normals = model.accessors[prim.attributes["NORMAL"]];
    Accessor& texCoords = model.accessors[prim.attributes["TEXCOORD_0"]];

    assert(indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    assert(indices.type == TINYGLTF_TYPE_SCALAR);

    targetMesh.indices.resize(indices.count);
    const uint16_t* indexData = ReadBuffer<uint16_t>(model, indices);
    for (size_t i = 0; i < indices.count; i++)
    {
        targetMesh.indices[i] = indexData[i];
    }

    targetMesh.vertices.resize(positions.count);
    for (size_t i = 0; i < positions.count; i++)
    {
        EnvironmentVertex& vertex = targetMesh.vertices[i];
        vertex.position = ReadBuffer<XrVector3f>(model, positions)[i];
        vertex.color = XrVector3f{ 1., 1., 1. };
        vertex.texCoord = ReadBuffer<XrVector2f>(model, texCoords)[i];
        //vertex.normal = ReadBuffer<XrVector3f>(model, normals)[i];
    }
}