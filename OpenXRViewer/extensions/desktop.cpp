#include "desktop.h"

#include "../../EquirectConverter/src/Memory.cpp"

#include <limits>

void Transition(ID3D12GraphicsCommandList* renderList, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, from, to);
    renderList->ResourceBarrier(1, &barrier);
}

void DesktopView::InitSwapchain(ID3D12Device* device, DXGI_FORMAT format, UINT width, UINT height)
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

void DesktopView::CopyRenderResultToPreview(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colorTexture, int frameIndex)
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

std::vector<XMVECTOR> RaySphereIntersection(XMVECTOR rayStart, XMVECTOR rayDirection, XMVECTOR sphereCenter, float sphereRadius)
{
    std::vector<XMVECTOR> output{};

    XMVECTOR raySphereOffset = rayStart - sphereCenter;

    float a = XMVectorGetX(XMVector3Dot(rayDirection, rayDirection));
    float b = 2.f * XMVectorGetX(XMVector3Dot(rayDirection, raySphereOffset));
    float c = XMVectorGetX(XMVector3Dot(raySphereOffset, raySphereOffset)) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.f * a * c;

    // TODO: test this part
    if (discriminant == 0.f) // TODO: use epsilon?
    {
		float t = -b / (2.f * a);
        if (t >= 0.f)
        {
            output.push_back(rayStart + rayDirection * t);
        }
	}
    else if (discriminant > 0.f)
    {
		float t1 = (-b + sqrtf(discriminant)) / (2.f * a);
        if (t1 >= 0.f)
        {
            output.push_back(rayStart + rayDirection * t1);
        }

		float t2 = (-b - sqrtf(discriminant)) / (2.f * a);
        if (t2 >= 0.f)
        {
            output.push_back(rayStart + rayDirection * t2);
        }
	}

    return output;
}

XMVECTOR WorldPosToEquirectangularTexturePos(XMVECTOR worldPos, size_t textureWidth, size_t textureHeight)
{
	XMVECTOR normalizedWorldPos = XMVector3Normalize(worldPos);
	float phi = atan2f(XMVectorGetZ(normalizedWorldPos), XMVectorGetX(normalizedWorldPos));
	float theta = acosf(XMVectorGetY(normalizedWorldPos));
	float u = phi / XM_2PI + 0.5f;
	float v = theta / XM_PI;
	return XMVectorSet(u * static_cast<float>(textureWidth), v * static_cast<float>(textureHeight), 0.f, 0.f);
}

void DesktopView::CreatePerfectFilteredImage(XMMATRIX spaceToView, XMMATRIX projection, size_t screenWidth, size_t screenHeight)
{
    MemoryArena arena{};
    uint8_t* imageData = NewArray(arena, uint8_t, screenWidth * screenHeight * 3);
    
    // load sphere texture with stb_image
    int sphereTextureWidth, sphereTextureHeight, sphereTextureChannelCount;
    uint8_t* sphereData = stbi_load("textures/Wolfstein.jpg", &sphereTextureWidth, &sphereTextureHeight, &sphereTextureChannelCount, 3);
    assert(sphereData != nullptr);
    if (sphereData == nullptr) return;

    for (int x = 0; x < screenWidth; x++)
    {
        for (int y = 0; y < screenHeight; y++)
        {
            float clipSpaceX = (static_cast<float>(x) / static_cast<float>(screenWidth)) * 2.f - 1.f;
            float clipSpaceY = (static_cast<float>(y) / static_cast<float>(screenHeight)) * 2.f - 1.f;
            XMVECTOR clipSpacePositionNear = XMVectorSet(clipSpaceX, clipSpaceY, 0.f, 1.f);
            XMVECTOR clipSpacePositionFar  = XMVectorSet(clipSpaceX, clipSpaceY, 1.f, 1.f);

            XMMATRIX inverseProjection = XMMatrixInverse(nullptr, projection);
            XMVECTOR viewSpacePositionNear = XMVector3TransformCoord(clipSpacePositionNear, inverseProjection);
            XMVECTOR viewSpacePositionFar  = XMVector3TransformCoord(clipSpacePositionFar, inverseProjection);

            XMMATRIX inverseView = XMMatrixInverse(nullptr, spaceToView);
            XMVECTOR worldSpacePositionNear = XMVector3Transform(viewSpacePositionNear, inverseView);
            XMVECTOR worldSpacePositionFar  = XMVector3Transform(viewSpacePositionFar, inverseView);
            XMVECTOR worldSpaceDirection = XMVector3Normalize(worldSpacePositionFar - worldSpacePositionNear);

            const float sphereRadius = 500.f;
            std::vector<XMVECTOR> intersections = RaySphereIntersection(worldSpacePositionNear, worldSpaceDirection, { 0.f, 0.f, 0.f }, sphereRadius);

            size_t index = (y * screenWidth + x) * 3;
            if (intersections.size() > 0)
            {
                XMVECTOR intersection = intersections[0];
                XMVECTOR samplePos = WorldPosToEquirectangularTexturePos(intersection, sphereTextureWidth, sphereTextureHeight);
                size_t sphereDataIndex = (static_cast<size_t>(XMVectorGetY(samplePos)) * sphereTextureWidth + static_cast<size_t>(XMVectorGetX(samplePos))) * 3;
                imageData[index] = sphereData[sphereDataIndex];
                imageData[index + 1] = sphereData[sphereDataIndex + 1];
                imageData[index + 2] = sphereData[sphereDataIndex + 2];
            }
            else
            {
                imageData[index] = 0;
                imageData[index + 1] = 0;
                imageData[index + 2] = 0;
            }
        }
    }

    stbi_write_png("perfect.png", screenWidth, screenHeight, 3, imageData, screenWidth * 3);
}

bool DesktopView::WriteFile(const char* name)
{
    if (previewTextures.size() == 0) return false;

    void* mappedMemory;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(width * height * 4) };
    D3D12_RANGE writeRange = { 0, 0 };
    previewTextures[0].cpuBuffer->Map(0, &readRange, &mappedMemory);
    auto sptr = static_cast<const uint8_t*>(mappedMemory);
    stbi_write_png(name, width, height, 4, sptr, width * 4);
    previewTextures[0].cpuBuffer->Unmap(0, &writeRange);
    return true;
}
