#pragma once

#include <vector>
#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
using namespace DirectX;
using Microsoft::WRL::ComPtr;

#undef min
#undef max

#include "../../EquirectConverter/libraries/stb/stb_image.h"
#include "../../EquirectConverter/libraries/stb/stb_image_write.h"

void Transition(ID3D12GraphicsCommandList* renderList, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);

struct PreviewTexture
{
    ComPtr<ID3D12Resource> gpuBuffer;
    ComPtr<ID3D12Resource> cpuBuffer;
};

class DesktopView
{
public:
    void InitSwapchain(ID3D12Device* device, DXGI_FORMAT format, UINT width, UINT height);
    void CopyRenderResultToPreview(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colorTexture, int frameIndex);
    void CreatePerfectFilteredImage(XMMATRIX spaceToView, XMMATRIX projection, size_t screenWidth, size_t screenHeight);
    bool WriteFile(const char* name);

public:
    bool wantWriteImage = true;

private:
    std::vector<PreviewTexture> previewTextures{};
    UINT width;
    UINT height;
};