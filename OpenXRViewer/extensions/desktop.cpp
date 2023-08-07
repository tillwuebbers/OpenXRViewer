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

void RaySphereIntersection(XMVECTOR rayStart, XMVECTOR rayDirection, XMVECTOR sphereCenter, float sphereRadius, std::vector<XMVECTOR>& outIntersections)
{
    outIntersections.clear();

    XMVECTOR raySphereOffset = rayStart - sphereCenter;

    float a = XMVectorGetX(XMVector3Dot(rayDirection, rayDirection));
    float b = 2.f * XMVectorGetX(XMVector3Dot(rayDirection, raySphereOffset));
    float c = XMVectorGetX(XMVector3Dot(raySphereOffset, raySphereOffset)) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.f * a * c;

    if (discriminant == 0.f) // TODO: use epsilon?
    {
		float t = -b / (2.f * a);
        if (t >= 0.f)
        {
            outIntersections.push_back(rayStart + rayDirection * t);
        }
    }
    else if (discriminant > 0.f)
    {
        float t1 = (-b + sqrtf(discriminant)) / (2.f * a);
        if (t1 >= 0.f)
        {
            outIntersections.push_back(rayStart + rayDirection * t1);
        }

        float t2 = (-b - sqrtf(discriminant)) / (2.f * a);
        if (t2 >= 0.f)
        {
            outIntersections.push_back(rayStart + rayDirection * t2);
        }
	}
}

void RayToSphere(int x, int y, XMMATRIX spaceToView, XMMATRIX projection, size_t screenWidth, size_t screenHeight, std::vector<XMVECTOR>& outIntersections)
{
    outIntersections.clear();

    float clipSpaceX = (static_cast<float>(x) / static_cast<float>(screenWidth)) * 2.f - 1.f;
    float clipSpaceY = (static_cast<float>(y) / static_cast<float>(screenHeight)) * 2.f - 1.f;
    XMVECTOR clipSpacePositionNear = XMVectorSet(clipSpaceX, clipSpaceY, 0.f, 1.f);
    XMVECTOR clipSpacePositionFar = XMVectorSet(clipSpaceX, clipSpaceY, 1.f, 1.f);

    XMMATRIX inverseProjection = XMMatrixInverse(nullptr, projection);
    XMVECTOR viewSpacePositionNear = XMVector3TransformCoord(clipSpacePositionNear, inverseProjection);
    XMVECTOR viewSpacePositionFar = XMVector3TransformCoord(clipSpacePositionFar, inverseProjection);

    XMMATRIX inverseView = XMMatrixInverse(nullptr, spaceToView);
    XMVECTOR worldSpacePositionNear = XMVector3Transform(viewSpacePositionNear, inverseView);
    XMVECTOR worldSpacePositionFar = XMVector3Transform(viewSpacePositionFar, inverseView);
    XMVECTOR worldSpaceDirection = XMVector3Normalize(worldSpacePositionFar - worldSpacePositionNear);

    const float sphereRadius = 500.f;
    RaySphereIntersection(worldSpacePositionNear, worldSpaceDirection, { 0.f, 0.f, 0.f }, sphereRadius, outIntersections);
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

// Check if a point p is on the "positive" side of the edge e1->e2
// result > 0 means p is on the positive side
// result < 0 means p is on the negative side
// result == 0 means p is on the edge
float EdgeFunction(const XMVECTOR& e1, const XMVECTOR& e2, const XMVECTOR& p)
{
    return (XMVectorGetX(p) - XMVectorGetX(e1)) * (XMVectorGetY(e2) - XMVectorGetY(e1)) - (XMVectorGetY(p) - XMVectorGetY(e1)) * (XMVectorGetX(e2) - XMVectorGetX(e1));
}

void DesktopView::CreatePerfectFilteredImage(XMMATRIX spaceToView, XMMATRIX projection, size_t screenWidth, size_t screenHeight)
{
    MemoryArena arena{};
    uint8_t* outputData = NewArray(arena, uint8_t, screenWidth * screenHeight * 3);
    
    // load sphere texture with stb_image
    int sampleTextureWidth, sampleTextureHeight, sampleTextureChannelCount;
    uint8_t* sampleData = stbi_load("textures/Wolfstein.jpg", &sampleTextureWidth, &sampleTextureHeight, &sampleTextureChannelCount, 3);
    assert(sampleData != nullptr);
    if (sampleData == nullptr) return;

    // Iterate output pixels
    #pragma omp parallel for
    for (int y = 0; y < screenHeight; y++)
    {
        std::vector<XMVECTOR> intersectionsTopLeft{};
        std::vector<XMVECTOR> intersectionsTopRight{};
        std::vector<XMVECTOR> intersectionsBotLeft{};
        std::vector<XMVECTOR> intersectionsBotRight{};

        for (int x = 0; x < screenWidth; x++)
        {
            size_t outputIndex = (y * screenWidth + x) * 3;

            // Intersect rays through corners of the pixel with sphere
            RayToSphere(x,     y,     spaceToView, projection, screenWidth, screenHeight, intersectionsTopLeft);
            RayToSphere(x + 1, y,     spaceToView, projection, screenWidth, screenHeight, intersectionsTopRight);
            RayToSphere(x,     y + 1, spaceToView, projection, screenWidth, screenHeight, intersectionsBotLeft);
            RayToSphere(x + 1, y + 1, spaceToView, projection, screenWidth, screenHeight, intersectionsBotRight);

            assert(intersectionsTopLeft.size() > 0 && intersectionsTopRight.size() > 0 && intersectionsBotLeft.size() > 0 && intersectionsBotRight.size() > 0);
            if (intersectionsTopLeft.size() == 0 || intersectionsTopRight.size() == 0 || intersectionsBotLeft.size() == 0 || intersectionsBotRight.size() == 0)
            {
                outputData[outputIndex] = 0;
                outputData[outputIndex + 1] = 0;
                outputData[outputIndex + 2] = 255;
                continue;
            }

            // Project intersections to sphere texture
            XMVECTOR sampleQuadVertices[4] = {
                WorldPosToEquirectangularTexturePos(intersectionsTopLeft[0],  sampleTextureWidth, sampleTextureHeight),
				WorldPosToEquirectangularTexturePos(intersectionsTopRight[0], sampleTextureWidth, sampleTextureHeight),
				WorldPosToEquirectangularTexturePos(intersectionsBotLeft[0],  sampleTextureWidth, sampleTextureHeight),
				WorldPosToEquirectangularTexturePos(intersectionsBotRight[0], sampleTextureWidth, sampleTextureHeight)
            };

            // Sort points on y axis
            std::qsort(sampleQuadVertices, _countof(sampleQuadVertices), sizeof(XMVECTOR), [](const void* a, const void* b) -> int {
                XMVECTOR aVec = *reinterpret_cast<const XMVECTOR*>(a);
				XMVECTOR bVec = *reinterpret_cast<const XMVECTOR*>(b);
                float ay = XMVectorGetY(aVec);
                float by = XMVectorGetY(bVec);
				if (ay < by) return -1;
                if (ay > by) return 1;
				return 0;
			});

            // Get bounds
            float minX = XMVectorGetX(sampleQuadVertices[0]);
            float minY = XMVectorGetY(sampleQuadVertices[0]);
            float maxX = minX;
            float maxY = minY;

            for (int i = 0; i < _countof(sampleQuadVertices); i++)
            {
                float x = XMVectorGetX(sampleQuadVertices[i]);
				float y = XMVectorGetY(sampleQuadVertices[i]);
                
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }

            maxX = std::min(maxX, static_cast<float>(sampleTextureWidth - 1));
            maxY = std::min(maxY, static_cast<float>(sampleTextureHeight - 1));

            // Declare both triangles of the quad in clockwise order
            if (XMVectorGetX(sampleQuadVertices[1]) > XMVectorGetX(sampleQuadVertices[2]))
            {
                std::swap(sampleQuadVertices[1], sampleQuadVertices[2]);
            }
            const XMVECTOR t1a = sampleQuadVertices[0];
            const XMVECTOR t1b = sampleQuadVertices[2];
            const XMVECTOR t1c = sampleQuadVertices[1];

            const XMVECTOR t2a = sampleQuadVertices[1];
            const XMVECTOR t2b = sampleQuadVertices[2];
            const XMVECTOR t2c = sampleQuadVertices[3];

            // Sum up all samples inside the two triangles
            size_t outR = 0;
            size_t outG = 0;
            size_t outB = 0;
            size_t sumCount = 0;

            const size_t subSampleCount = 8;
            for (size_t subSampleY = minY * subSampleCount; subSampleY <= maxY * subSampleCount; subSampleY++)
            {
                for (size_t subSampleX = minX * subSampleCount; subSampleX <= maxX * subSampleCount; subSampleX++)
                {
                    const XMVECTOR pixel = XMVectorSet(static_cast<float>(subSampleX) / subSampleCount + .5f, static_cast<float>(subSampleY) / subSampleCount + .5f, 0.f, 0.f);
                    if (   (EdgeFunction(t1a, t1b, pixel) >= 0.f && EdgeFunction(t1b, t1c, pixel) >= 0.f && EdgeFunction(t1c, t1a, pixel) >= 0.f)
                        || (EdgeFunction(t2a, t2b, pixel) >= 0.f && EdgeFunction(t2b, t2c, pixel) >= 0.f && EdgeFunction(t2c, t2a, pixel) >= 0.f))
                    {
                        size_t sampleTexIndex = ((subSampleY / subSampleCount) * sampleTextureWidth + (subSampleX / subSampleCount)) * 3;
                        outR += static_cast<uint8_t>(pow(static_cast<double>(sampleData[sampleTexIndex]) / 255., 1. / 2.2) * 255.);
                        outG += static_cast<uint8_t>(pow(static_cast<double>(sampleData[sampleTexIndex + 1]) / 255., 1. / 2.2) * 255.);
                        outB += static_cast<uint8_t>(pow(static_cast<double>(sampleData[sampleTexIndex + 2]) / 255., 1. / 2.2) * 255.);
                        sumCount += 1;
                    }
                }
            }
            
            // Average sample results
            //assert(sumCount > 0);
            if (sumCount == 0)
            {
                size_t centerX = (minX + maxX) / 2.f;
                size_t centerY = (minY + maxY) / 2.f;
                size_t sampleTexIndex = (centerY * sampleTextureWidth + centerX) * 3;
                outputData[outputIndex]     = pow(sampleData[sampleTexIndex]     / 255., 1. / 2.2) * 255.;
                outputData[outputIndex + 1] = pow(sampleData[sampleTexIndex + 1] / 255., 1. / 2.2) * 255.;
                outputData[outputIndex + 2] = pow(sampleData[sampleTexIndex + 2] / 255., 1. / 2.2) * 255.;
            }
            else
            {
                outputData[outputIndex] = static_cast<uint8_t>(outR / sumCount);
                outputData[outputIndex + 1] = static_cast<uint8_t>(outG / sumCount);
                outputData[outputIndex + 2] = static_cast<uint8_t>(outB / sumCount);
            }
        }
    }

    stbi_write_png("comparison_perfect_raytraced.png", screenWidth, screenHeight, 3, outputData, screenWidth * 3);
    //stbi_write_png("sampled-texture.png", sampleTextureWidth, sampleTextureHeight, 3, sampleData, sampleTextureWidth * 3);
    OutputDebugStringA("Done!\n");
    exit(0);
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
