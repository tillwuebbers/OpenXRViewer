#pragma once

#include <vector>
#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
using Microsoft::WRL::ComPtr;

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
    void InitSwapchain(ID3D12Device* device, DXGI_FORMAT format, UINT width, UINT height)
    {
        // Create preview textures
        PreviewTexture& previewTexture = previewTextures.emplace_back();

        this->width = width;
        this->height = height;
        size_t bufferSize = width * height * 4;

        // gpu buffer
        D3D12_HEAP_PROPERTIES defaultHeapProperties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT) };
        D3D12_RESOURCE_DESC outputBufferDesc{ CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) };
        device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &outputBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&previewTexture.gpuBuffer));

        D3D12_HEAP_PROPERTIES readbackHeapProperties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK) };
        D3D12_RESOURCE_DESC readbackBufferDesc{ CD3DX12_RESOURCE_DESC::Buffer(bufferSize) };
        device->CreateCommittedResource(&readbackHeapProperties, D3D12_HEAP_FLAG_NONE, &readbackBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&previewTexture.cpuBuffer));
    }

	void CopyRenderResultToPreview(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colorTexture, int frameIndex)
	{
        PreviewTexture& previewTexture = previewTextures.at(frameIndex);

        Transition(cmdList, previewTexture.gpuBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(cmdList, colorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
        bufferFootprint.Footprint.Width = width;
        bufferFootprint.Footprint.Height = height;
        bufferFootprint.Footprint.Depth = 1;
        bufferFootprint.Footprint.RowPitch = width * 4;
        bufferFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        CD3DX12_TEXTURE_COPY_LOCATION dst(previewTexture.gpuBuffer.Get(), bufferFootprint);
        CD3DX12_TEXTURE_COPY_LOCATION src(colorTexture, 0);
        cmdList->CopyTextureRegion(&dst, /*todo: i * m_xrState.m_previewWidth / 2 */ 0, 0, 0, &src, nullptr);

        Transition(cmdList, colorTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(cmdList, previewTexture.gpuBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

        cmdList->CopyResource(previewTexture.cpuBuffer.Get(), previewTexture.gpuBuffer.Get());
	}

    void WriteFile()
    {
        if (previewTextures.size() == 0) return;

        void* mappedMemory;
        D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(width * height * 4) };
        D3D12_RANGE writeRange = { 0, 0 };
        previewTextures[0].cpuBuffer->Map(0, &readRange, &mappedMemory);
        auto sptr = static_cast<const uint8_t*>(mappedMemory);
        stbi_write_bmp("test_bias_10.bmp", width, height, 4, sptr);
        previewTextures[0].cpuBuffer->Unmap(0, &writeRange);
    }

private:
    std::vector<PreviewTexture> previewTextures{};
    UINT width;
    UINT height;
};