#pragma once

#include <vector>
#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
using Microsoft::WRL::ComPtr;

void Transition(ID3D12GraphicsCommandList* renderList, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);

struct PreviewTexture
{
    ComPtr<ID3D12Resource> buffer;
};

class DesktopView
{
public:
    void InitSwapchain(ID3D12Device* device, DXGI_FORMAT format, UINT width, UINT height)
    {
        // Create preview textures
        PreviewTexture& previewTexture = previewTextures.emplace_back();

        D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&previewTexture.buffer));
        previewTexture.buffer->SetName(L"Desktop Preview Texture");

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        //device->CreateShaderResourceView(previewTexture.buffer.Get(), &srvDesc, previewTexture.cpuHandle);
    }

	void CopyRenderResultToPreview(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colorTexture, int frameIndex)
	{
        ID3D12Resource* previewTexture = previewTextures.at(frameIndex).buffer.Get();

        Transition(cmdList, previewTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(cmdList, colorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        CD3DX12_TEXTURE_COPY_LOCATION dst(previewTexture);
        CD3DX12_TEXTURE_COPY_LOCATION src(colorTexture);
        cmdList->CopyTextureRegion(&dst, /*todo: i * m_xrState.m_previewWidth / 2 */ 0, 0, 0, &src, nullptr);

        Transition(cmdList, colorTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(cmdList, previewTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

private:
    std::vector<PreviewTexture> previewTextures{};
};