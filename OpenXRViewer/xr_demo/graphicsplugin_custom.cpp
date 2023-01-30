// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0


#include "pch.h"
#include "common.h"
#include "geometry.h"
#include "graphicsplugin.h"
#include "options.h"

#include "../desktop/desktop.h"

#if defined(USE_CUSTOM_GRAPHICS_PLUGIN) && defined(XR_USE_GRAPHICS_API_D3D12) && !defined(MISSING_DIRECTX_COLORS)

#include "xr_linear.h"
#include <DirectXColors.h>
#include <D3Dcompiler.h>

#include "d3d_common.h"

#undef min
#undef max

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#include "../import/tiny_gltf.h"
using namespace tinygltf;

using namespace Microsoft::WRL;
using namespace DirectX;

namespace {
    struct SphereVertex
    {
        XrVector3f position;
        XrVector3f color;
        XrVector2f texCoord;
        //XrVector3f normal;
    };

    struct SphereMesh
    {
		std::vector<SphereVertex> vertices;
		std::vector<uint16_t> indices;
    };

    template <typename T>
    const T* ReadBuffer(Model& model, Accessor& accessor)
    {
        BufferView& bufferView = model.bufferViews[accessor.bufferView];
        Buffer& buffer = model.buffers[bufferView.buffer];
        return reinterpret_cast<T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
    }

    void LoadSphere(SphereMesh& mesh)
    {
        Model model;
        TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, "models/sphere.glb");

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
		Accessor& normals = model.accessors[prim.attributes["NORMAL"]];
		Accessor& texCoords = model.accessors[prim.attributes["TEXCOORD_0"]];

        assert(indices.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
        assert(indices.type == TINYGLTF_TYPE_SCALAR);

        mesh.indices.resize(indices.count);
        const uint16_t* indexData = ReadBuffer<uint16_t>(model, indices);
		for (size_t i = 0; i < indices.count; i++)
		{
			mesh.indices[i] = indexData[i];
		}

        mesh.vertices.resize(positions.count);
		for (size_t i = 0; i < positions.count; i++)
		{
			SphereVertex& vertex = mesh.vertices[i];
			vertex.position = ReadBuffer<XrVector3f>(model, positions)[i];
            vertex.color = XrVector3f{1., 1., 1.};
			vertex.texCoord = ReadBuffer<XrVector2f>(model, texCoords)[i];
            //vertex.normal = ReadBuffer<XrVector3f>(model, normals)[i];
		}
    }

    void InitializeD3D12DeviceForAdapter(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, ID3D12Device** device) {
#if !defined(NDEBUG)
        ComPtr<ID3D12Debug> debugCtrl;
        if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), &debugCtrl))) {
            debugCtrl->EnableDebugLayer();
        }
#endif

        CHECK_HRCMD(D3D12CreateDevice(adapter, minimumFeatureLevel, __uuidof(ID3D12Device), reinterpret_cast<void**>(device)));
    }

    template <uint32_t alignment>
    constexpr uint32_t AlignTo(uint32_t n) {
        static_assert((alignment & (alignment - 1)) == 0, "The alignment must be power-of-two");
        return (n + alignment - 1) & ~(alignment - 1);
    }

    ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* d3d12Device, uint32_t size, D3D12_HEAP_TYPE heapType) {
        D3D12_RESOURCE_STATES d3d12ResourceState;
        if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
            d3d12ResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
            size = AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(size);
        }
        else {
            d3d12ResourceState = D3D12_RESOURCE_STATE_COMMON;
        }

        D3D12_HEAP_PROPERTIES heapProp{};
        heapProp.Type = heapType;
        heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC buffDesc{};
        buffDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffDesc.Alignment = 0;
        buffDesc.Width = size;
        buffDesc.Height = 1;
        buffDesc.DepthOrArraySize = 1;
        buffDesc.MipLevels = 1;
        buffDesc.Format = DXGI_FORMAT_UNKNOWN;
        buffDesc.SampleDesc.Count = 1;
        buffDesc.SampleDesc.Quality = 0;
        buffDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        CHECK_HRCMD(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &buffDesc, d3d12ResourceState, nullptr,
            __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(buffer.ReleaseAndGetAddressOf())));
        return buffer;
    }

    ComPtr<ID3D12Resource> CreateTexture(ID3D12Device* d3d12Device, uint32_t w, uint32_t h, D3D12_HEAP_TYPE heapType) {
        D3D12_RESOURCE_STATES d3d12ResourceState = D3D12_RESOURCE_STATE_COMMON;

        D3D12_HEAP_PROPERTIES heapProp{};
        heapProp.Type = heapType;
        heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC buffDesc{};
        buffDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        buffDesc.Alignment = 0;
        buffDesc.Width = w;
        buffDesc.Height = h;
        buffDesc.DepthOrArraySize = 1;
        buffDesc.MipLevels = 1;
        buffDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        buffDesc.SampleDesc.Count = 1;
        buffDesc.SampleDesc.Quality = 0;
        buffDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        buffDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        CHECK_HRCMD(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &buffDesc, d3d12ResourceState, nullptr,
            __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(buffer.ReleaseAndGetAddressOf())));
        return buffer;
    }

    class SwapchainImageContext {
    public:
        std::vector<XrSwapchainImageBaseHeader*> Create(ID3D12Device* d3d12Device, uint32_t capacity) {
            m_d3d12Device = d3d12Device;

            m_swapchainImages.resize(capacity);
            std::vector<XrSwapchainImageBaseHeader*> bases(capacity);
            for (uint32_t i = 0; i < capacity; ++i) {
                m_swapchainImages[i] = { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR };
                bases[i] = reinterpret_cast<XrSwapchainImageBaseHeader*>(&m_swapchainImages[i]);
            }

            CHECK_HRCMD(m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void**>(m_commandAllocator.ReleaseAndGetAddressOf())));

            m_viewProjectionCBuffer = CreateBuffer(m_d3d12Device, sizeof(ViewProjectionConstantBuffer), D3D12_HEAP_TYPE_UPLOAD);

            return bases;
        }

        uint32_t ImageIndex(const XrSwapchainImageBaseHeader* swapchainImageHeader) {
            auto p = reinterpret_cast<const XrSwapchainImageD3D12KHR*>(swapchainImageHeader);
            return (uint32_t)(p - &m_swapchainImages[0]);
        }

        ID3D12Resource* GetDepthStencilTexture(ID3D12Resource* colorTexture) {
            if (!m_depthStencilTexture) {
                // This back-buffer has no corresponding depth-stencil texture, so create one with matching dimensions.

                const D3D12_RESOURCE_DESC colorDesc = colorTexture->GetDesc();

                D3D12_HEAP_PROPERTIES heapProp{};
                heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
                heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

                D3D12_RESOURCE_DESC depthDesc{};
                depthDesc.Dimension = colorDesc.Dimension;
                depthDesc.Alignment = colorDesc.Alignment;
                depthDesc.Width = colorDesc.Width;
                depthDesc.Height = colorDesc.Height;
                depthDesc.DepthOrArraySize = colorDesc.DepthOrArraySize;
                depthDesc.MipLevels = 1;
                depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                depthDesc.SampleDesc.Count = 1;
                depthDesc.Layout = colorDesc.Layout;
                depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

                D3D12_CLEAR_VALUE clearValue{};
                clearValue.DepthStencil.Depth = 1.0f;
                clearValue.Format = DXGI_FORMAT_D32_FLOAT;

                CHECK_HRCMD(m_d3d12Device->CreateCommittedResource(
                    &heapProp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                    __uuidof(ID3D12Resource), reinterpret_cast<void**>(m_depthStencilTexture.ReleaseAndGetAddressOf())));
            }

            return m_depthStencilTexture.Get();
        }

        ID3D12CommandAllocator* GetCommandAllocator() const { return m_commandAllocator.Get(); }

        uint64_t GetFrameFenceValue() const { return m_fenceValue; }
        void SetFrameFenceValue(uint64_t fenceValue) { m_fenceValue = fenceValue; }

        void ResetCommandAllocator() { CHECK_HRCMD(m_commandAllocator->Reset()); }

        void RequestModelCBuffer(uint32_t requiredSize) {
            if (!m_modelCBuffer || (requiredSize > m_modelCBuffer->GetDesc().Width)) {
                m_modelCBuffer = CreateBuffer(m_d3d12Device, requiredSize, D3D12_HEAP_TYPE_UPLOAD);
            }
        }

        ID3D12Resource* GetModelCBuffer() const { return m_modelCBuffer.Get(); }
        ID3D12Resource* GetViewProjectionCBuffer() const { return m_viewProjectionCBuffer.Get(); }

    private:
        ID3D12Device* m_d3d12Device{ nullptr };

        std::vector<XrSwapchainImageD3D12KHR> m_swapchainImages;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator;
        ComPtr<ID3D12Resource> m_depthStencilTexture;
        ComPtr<ID3D12Resource> m_modelCBuffer;
        ComPtr<ID3D12Resource> m_viewProjectionCBuffer;
        uint64_t m_fenceValue = 0;
    };

    // Separate entrypoints for the vertex and pixel shader functions.
    constexpr char CustomShaderHlsl[] = R"_(
struct PSVertex {
    float4 Pos : SV_POSITION;
    float3 Color : COLOR0;
    float2 UV : TEXCOORD0;
};
struct Vertex {
    float3 Pos : POSITION;
    float3 Color : COLOR0;
    float2 UV : TEXCOORD0;
};
cbuffer ModelConstantBuffer : register(b0) {
    float4x4 Model;
};
cbuffer ViewProjectionConstantBuffer : register(b1) {
    float4x4 ViewProjection;
};
Texture2D tex : register(t2);
SamplerState texSampler : register(s0);

PSVertex MainVS(Vertex input) {
    PSVertex output;
    output.Pos = mul(mul(float4(input.Pos, 1), Model), ViewProjection);
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}

float4 MainPS(PSVertex input) : SV_TARGET {
    return tex.Sample(texSampler, input.UV);
}
    )_";

    struct D3D12GraphicsPlugin : public IGraphicsPlugin {
        D3D12GraphicsPlugin(const std::shared_ptr<Options>& options, std::shared_ptr<IPlatformPlugin>)
            : m_vertexShaderBytes(CompileShader(CustomShaderHlsl, "MainVS", "vs_5_1")),
            m_pixelShaderBytes(CompileShader(CustomShaderHlsl, "MainPS", "ps_5_1")),
            m_clearColor(options->GetBackgroundClearColor()) {}

        ~D3D12GraphicsPlugin() override { CloseHandle(m_fenceEvent); }

        std::vector<std::string> GetInstanceExtensions() const override { return { XR_KHR_D3D12_ENABLE_EXTENSION_NAME }; }

        void InitializeDevice(XrInstance instance, XrSystemId systemId) override {
            PFN_xrGetD3D12GraphicsRequirementsKHR pfnGetD3D12GraphicsRequirementsKHR = nullptr;
            CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetD3D12GraphicsRequirementsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetD3D12GraphicsRequirementsKHR)));

            // Create the D3D12 device for the adapter associated with the system.
            XrGraphicsRequirementsD3D12KHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
            CHECK_XRCMD(pfnGetD3D12GraphicsRequirementsKHR(instance, systemId, &graphicsRequirements));
            const ComPtr<IDXGIAdapter1> adapter = GetAdapter(graphicsRequirements.adapterLuid);

            // Create a list of feature levels which are both supported by the OpenXR runtime and this application.
            InitializeD3D12DeviceForAdapter(adapter.Get(), graphicsRequirements.minFeatureLevel, m_device.ReleaseAndGetAddressOf());

            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            CHECK_HRCMD(m_device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void**>(m_cmdQueue.ReleaseAndGetAddressOf())));

            InitializeResources();

            m_graphicsBinding.device = m_device.Get();
            m_graphicsBinding.queue = m_cmdQueue.Get();
        }

        void InitializeResources() {
            {
                D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
                heapDesc.NumDescriptors = 1;
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                    reinterpret_cast<void**>(m_rtvHeap.ReleaseAndGetAddressOf())));
            }
            {
                D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
                heapDesc.NumDescriptors = 1;
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                    reinterpret_cast<void**>(m_dsvHeap.ReleaseAndGetAddressOf())));
            }
            {
                D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
                heapDesc.NumDescriptors = 1;
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                CHECK_HRCMD(m_device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                    reinterpret_cast<void**>(m_cbvSrvHeap.ReleaseAndGetAddressOf())));
            }

            std::vector<D3D12_DESCRIPTOR_RANGE> ranges{ 1 };
			ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			ranges[0].NumDescriptors = 1;
			ranges[0].BaseShaderRegister = 2;
			ranges[0].RegisterSpace = 0;
			ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER rootParams[3];
            rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[0].Descriptor.ShaderRegister = 0;
            rootParams[0].Descriptor.RegisterSpace = 0;
            rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[1].Descriptor.ShaderRegister = 1;
            rootParams[1].Descriptor.RegisterSpace = 0;
            rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
			rootParams[2].DescriptorTable.pDescriptorRanges = ranges.data();
			rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

            D3D12_STATIC_SAMPLER_DESC texSampler = {};
            texSampler.Filter = D3D12_FILTER_ANISOTROPIC;
            texSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            texSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            texSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            texSampler.MipLODBias = 0;
            texSampler.MaxAnisotropy = 16;
            texSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            texSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            texSampler.MinLOD = 0.0f;
            texSampler.MaxLOD = D3D12_FLOAT32_MAX;
            texSampler.ShaderRegister = 0;
            texSampler.RegisterSpace = 0;
            texSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            std::vector<D3D12_STATIC_SAMPLER_DESC> samplers = { texSampler };

            D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
            rootSignatureDesc.NumParameters = (UINT)ArraySize(rootParams);
            rootSignatureDesc.pParameters = rootParams;
            rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			rootSignatureDesc.NumStaticSamplers = (UINT)samplers.size();
			rootSignatureDesc.pStaticSamplers = samplers.data();

            ComPtr<ID3DBlob> rootSignatureBlob;
            ComPtr<ID3DBlob> error;
            CHECK_HRCMD(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                rootSignatureBlob.ReleaseAndGetAddressOf(), error.ReleaseAndGetAddressOf()));

            CHECK_HRCMD(m_device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(),
                __uuidof(ID3D12RootSignature),
                reinterpret_cast<void**>(m_rootSignature.ReleaseAndGetAddressOf())));

            SwapchainImageContext initializeContext;
            std::vector<XrSwapchainImageBaseHeader*> _ = initializeContext.Create(m_device.Get(), 1);

            ComPtr<ID3D12GraphicsCommandList> cmdList;
            CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, initializeContext.GetCommandAllocator(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

            // Upload cubes
            ComPtr<ID3D12Resource> cubeVertexBufferUpload;
            m_cubeVertexBuffer = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeVertices), D3D12_HEAP_TYPE_DEFAULT);
            {
                cubeVertexBufferUpload = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeVertices), D3D12_HEAP_TYPE_UPLOAD);

                void* data;
                const D3D12_RANGE readRange{ 0, 0 };
                CHECK_HRCMD(cubeVertexBufferUpload->Map(0, &readRange, &data));
                memcpy(data, Geometry::c_cubeVertices, sizeof(Geometry::c_cubeVertices));
                cubeVertexBufferUpload->Unmap(0, nullptr);

                cmdList->CopyBufferRegion(m_cubeVertexBuffer.Get(), 0, cubeVertexBufferUpload.Get(), 0, sizeof(Geometry::c_cubeVertices));
            }

            ComPtr<ID3D12Resource> cubeIndexBufferUpload;
            m_cubeIndexBuffer = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeIndices), D3D12_HEAP_TYPE_DEFAULT);
            {
                cubeIndexBufferUpload = CreateBuffer(m_device.Get(), sizeof(Geometry::c_cubeIndices), D3D12_HEAP_TYPE_UPLOAD);

                void* data;
                const D3D12_RANGE readRange{ 0, 0 };
                CHECK_HRCMD(cubeIndexBufferUpload->Map(0, &readRange, &data));
                memcpy(data, Geometry::c_cubeIndices, sizeof(Geometry::c_cubeIndices));
                cubeIndexBufferUpload->Unmap(0, nullptr);

                cmdList->CopyBufferRegion(m_cubeIndexBuffer.Get(), 0, cubeIndexBufferUpload.Get(), 0, sizeof(Geometry::c_cubeIndices));
            }

            // Load and upload sphere
            SphereMesh sphere{};
            LoadSphere(sphere);
            m_sphereVertexCount = sphere.vertices.size();
            m_sphereIndexCount = sphere.indices.size();

            ComPtr<ID3D12Resource> sphereVertexBufferUpload;
			size_t sphereVertexBufferSize = sphere.vertices.size() * sizeof(SphereVertex);
            m_sphereVertexBuffer = CreateBuffer(m_device.Get(), sphereVertexBufferSize, D3D12_HEAP_TYPE_DEFAULT);
            {
                sphereVertexBufferUpload = CreateBuffer(m_device.Get(), sphereVertexBufferSize, D3D12_HEAP_TYPE_UPLOAD);

                void* data;
                const D3D12_RANGE readRange{ 0, 0 };
                CHECK_HRCMD(sphereVertexBufferUpload->Map(0, &readRange, &data));
                memcpy(data, sphere.vertices.data(), sphereVertexBufferSize);
                sphereVertexBufferUpload->Unmap(0, nullptr);

                cmdList->CopyBufferRegion(m_sphereVertexBuffer.Get(), 0, sphereVertexBufferUpload.Get(), 0, sphereVertexBufferSize);
            }

            ComPtr<ID3D12Resource> sphereIndexBufferUpload;
			size_t sphereIndexBufferSize = sphere.indices.size() * sizeof(uint16_t);
            m_sphereIndexBuffer = CreateBuffer(m_device.Get(), sphereIndexBufferSize, D3D12_HEAP_TYPE_DEFAULT);
            {
                sphereIndexBufferUpload = CreateBuffer(m_device.Get(), sphereIndexBufferSize, D3D12_HEAP_TYPE_UPLOAD);

                void* data;
                const D3D12_RANGE readRange{ 0, 0 };
                CHECK_HRCMD(sphereIndexBufferUpload->Map(0, &readRange, &data));
                memcpy(data, sphere.indices.data(), sphereIndexBufferSize);
                sphereIndexBufferUpload->Unmap(0, nullptr);

                cmdList->CopyBufferRegion(m_sphereIndexBuffer.Get(), 0, sphereIndexBufferUpload.Get(), 0, sphereIndexBufferSize);
            }

            // Upload Texture
            ComPtr<ID3D12Resource> textureUpload;
			{
				std::vector<uint8_t> textureData;
				int textureWidth, textureHeight, textureChannels;
				stbi_uc* pixels = stbi_load("textures/Wolfstein.jpg", &textureWidth, &textureHeight, &textureChannels, STBI_rgb_alpha);
				CHECK(pixels != nullptr);
				textureData.resize(textureWidth * textureHeight * 4);
				memcpy(textureData.data(), pixels, textureData.size());
				stbi_image_free(pixels);

				m_texture = CreateTexture(m_device.Get(), textureWidth, textureHeight, D3D12_HEAP_TYPE_DEFAULT);
				{
					textureUpload = CreateBuffer(m_device.Get(), textureData.size(), D3D12_HEAP_TYPE_UPLOAD);

					void* data;
					const D3D12_RANGE readRange{ 0, 0 };
					CHECK_HRCMD(textureUpload->Map(0, &readRange, &data));
					memcpy(data, textureData.data(), textureData.size());
					textureUpload->Unmap(0, nullptr);
                    
                    // Copy texture
					D3D12_TEXTURE_COPY_LOCATION dst = {};
					dst.pResource = m_texture.Get();
					dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					dst.SubresourceIndex = 0;
                    
					D3D12_TEXTURE_COPY_LOCATION src = {};
					src.pResource = textureUpload.Get();
					src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					src.PlacedFootprint.Offset = 0;
					D3D12_RESOURCE_DESC textureDesc = m_texture->GetDesc();
					
					src.PlacedFootprint.Footprint.Format = textureDesc.Format;
					src.PlacedFootprint.Footprint.Width = textureDesc.Width;
					src.PlacedFootprint.Footprint.Height = textureDesc.Height;
					src.PlacedFootprint.Footprint.Depth = 1;
					src.PlacedFootprint.Footprint.RowPitch = (textureDesc.Width * 4 + 255) & ~255;
                    
					cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                    
				}

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
			}
            
            CHECK_HRCMD(cmdList->Close());
            ID3D12CommandList* cmdLists[] = { cmdList.Get() };
            m_cmdQueue->ExecuteCommandLists((UINT)ArraySize(cmdLists), cmdLists);

            CHECK_HRCMD(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void**>(m_fence.ReleaseAndGetAddressOf())));
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            CHECK(m_fenceEvent != nullptr);

            WaitForGpu();
        }

        int64_t SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) const override {
            // List of supported color swapchain formats.
            constexpr DXGI_FORMAT SupportedColorSwapchainFormats[] = {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            };

            auto swapchainFormatIt =
                std::find_first_of(runtimeFormats.begin(), runtimeFormats.end(), std::begin(SupportedColorSwapchainFormats),
                    std::end(SupportedColorSwapchainFormats));
            if (swapchainFormatIt == runtimeFormats.end()) {
                THROW("No runtime swapchain format supported for color swapchain");
            }

            return *swapchainFormatIt;
        }

        const XrBaseInStructure* GetGraphicsBinding() const override {
            return reinterpret_cast<const XrBaseInStructure*>(&m_graphicsBinding);
        }

        std::vector<XrSwapchainImageBaseHeader*> AllocateSwapchainImageStructs(
            uint32_t capacity, const XrSwapchainCreateInfo& /*swapchainCreateInfo*/) override {
            // Allocate and initialize the buffer of image structs (must be sequential in memory for xrEnumerateSwapchainImages).
            // Return back an array of pointers to each swapchain image struct so the consumer doesn't need to know the type/size.

            m_swapchainImageContexts.emplace_back();
            SwapchainImageContext& swapchainImageContext = m_swapchainImageContexts.back();

            std::vector<XrSwapchainImageBaseHeader*> bases = swapchainImageContext.Create(m_device.Get(), capacity);

            // Map every swapchainImage base pointer to this context
            for (auto& base : bases) {
                m_swapchainImageContextMap[base] = &swapchainImageContext;
            }

            m_maxSwapchainImageCount = std::max(m_maxSwapchainImageCount, capacity);
            
            return bases;
        }

        ID3D12PipelineState* GetOrCreatePipelineState(DXGI_FORMAT swapchainFormat) {
            auto iter = m_pipelineStates.find(swapchainFormat);
            if (iter != m_pipelineStates.end()) {
                return iter->second.Get();
            }

            const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR"   , 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
            pipelineStateDesc.pRootSignature = m_rootSignature.Get();
            pipelineStateDesc.VS = { m_vertexShaderBytes->GetBufferPointer(), m_vertexShaderBytes->GetBufferSize() };
            pipelineStateDesc.PS = { m_pixelShaderBytes->GetBufferPointer(), m_pixelShaderBytes->GetBufferSize() };
            {
                pipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
                pipelineStateDesc.BlendState.IndependentBlendEnable = false;

                for (size_t i = 0; i < ArraySize(pipelineStateDesc.BlendState.RenderTarget); ++i) {
                    pipelineStateDesc.BlendState.RenderTarget[i].BlendEnable = false;

                    pipelineStateDesc.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
                    pipelineStateDesc.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
                    pipelineStateDesc.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;

                    pipelineStateDesc.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
                    pipelineStateDesc.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
                    pipelineStateDesc.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;

                    pipelineStateDesc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
                    pipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                }
            }
            pipelineStateDesc.SampleMask = 0xFFFFFFFF;
            {
                pipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
                pipelineStateDesc.RasterizerState.FrontCounterClockwise = FALSE;
                pipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
                pipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
                pipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
                pipelineStateDesc.RasterizerState.DepthClipEnable = TRUE;
                pipelineStateDesc.RasterizerState.MultisampleEnable = FALSE;
                pipelineStateDesc.RasterizerState.AntialiasedLineEnable = FALSE;
                pipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
                pipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            }
            {
                pipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
                pipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                pipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
                pipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
                pipelineStateDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
                pipelineStateDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
                pipelineStateDesc.DepthStencilState.FrontFace = pipelineStateDesc.DepthStencilState.BackFace = {
                    D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
            }
            {
                pipelineStateDesc.InputLayout.pInputElementDescs = inputElementDescs;
                pipelineStateDesc.InputLayout.NumElements = (UINT)ArraySize(inputElementDescs);
            }
            pipelineStateDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
            pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipelineStateDesc.NumRenderTargets = 1;
            pipelineStateDesc.RTVFormats[0] = swapchainFormat;
            pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            pipelineStateDesc.SampleDesc = { 1, 0 };
            pipelineStateDesc.NodeMask = 0;
            pipelineStateDesc.CachedPSO = { nullptr, 0 };
            pipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

            ComPtr<ID3D12PipelineState> pipelineState;
            CHECK_HRCMD(m_device->CreateGraphicsPipelineState(&pipelineStateDesc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void**>(pipelineState.ReleaseAndGetAddressOf())));
            ID3D12PipelineState* pipelineStateRaw = pipelineState.Get();

            m_pipelineStates.emplace(swapchainFormat, std::move(pipelineState));

            return pipelineStateRaw;
        }

        void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage,
            int64_t swapchainFormat, const std::vector<Cube>& cubes) override {
            CHECK(layerView.subImage.imageArrayIndex == 0);  // Texture arrays not supported.

            auto& swapchainContext = *m_swapchainImageContextMap[swapchainImage];
            CpuWaitForFence(swapchainContext.GetFrameFenceValue());
            swapchainContext.ResetCommandAllocator();

            ComPtr<ID3D12GraphicsCommandList> cmdList;
            CHECK_HRCMD(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, swapchainContext.GetCommandAllocator(), nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void**>(cmdList.ReleaseAndGetAddressOf())));

            ID3D12PipelineState* pipelineState = GetOrCreatePipelineState((DXGI_FORMAT)swapchainFormat);
            cmdList->SetPipelineState(pipelineState);
            cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

            ID3D12Resource* const colorTexture = reinterpret_cast<const XrSwapchainImageD3D12KHR*>(swapchainImage)->texture;
            const D3D12_RESOURCE_DESC colorTextureDesc = colorTexture->GetDesc();

            const D3D12_VIEWPORT viewport = { (float)layerView.subImage.imageRect.offset.x,
                                             (float)layerView.subImage.imageRect.offset.y,
                                             (float)layerView.subImage.imageRect.extent.width,
                                             (float)layerView.subImage.imageRect.extent.height,
                                             0,
                                             1 };
            cmdList->RSSetViewports(1, &viewport);

            const D3D12_RECT scissorRect = { layerView.subImage.imageRect.offset.x, layerView.subImage.imageRect.offset.y,
                                            layerView.subImage.imageRect.offset.x + layerView.subImage.imageRect.extent.width,
                                            layerView.subImage.imageRect.offset.y + layerView.subImage.imageRect.extent.height };
            cmdList->RSSetScissorRects(1, &scissorRect);

            // Create RenderTargetView with original swapchain format (swapchain is typeless).
            D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
            renderTargetViewDesc.Format = (DXGI_FORMAT)swapchainFormat;
            if (colorTextureDesc.DepthOrArraySize > 1) {
                if (colorTextureDesc.SampleDesc.Count > 1) {
                    renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                    renderTargetViewDesc.Texture2DMSArray.ArraySize = colorTextureDesc.DepthOrArraySize;
                }
                else {
                    renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    renderTargetViewDesc.Texture2DArray.ArraySize = colorTextureDesc.DepthOrArraySize;
                }
            }
            else {
                if (colorTextureDesc.SampleDesc.Count > 1) {
                    renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
                }
                else {
                    renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                }
            }
            m_device->CreateRenderTargetView(colorTexture, &renderTargetViewDesc, renderTargetView);

            ID3D12Resource* depthStencilTexture = swapchainContext.GetDepthStencilTexture(colorTexture);
            const D3D12_RESOURCE_DESC depthStencilTextureDesc = depthStencilTexture->GetDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
            depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
            if (depthStencilTextureDesc.DepthOrArraySize > 1) {
                if (depthStencilTextureDesc.SampleDesc.Count > 1) {
                    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
                    depthStencilViewDesc.Texture2DMSArray.ArraySize = colorTextureDesc.DepthOrArraySize;
                }
                else {
                    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    depthStencilViewDesc.Texture2DArray.ArraySize = colorTextureDesc.DepthOrArraySize;
                }
            }
            else {
                if (depthStencilTextureDesc.SampleDesc.Count > 1) {
                    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
                }
                else {
                    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                }
            }
            m_device->CreateDepthStencilView(depthStencilTexture, &depthStencilViewDesc, depthStencilView);

            // Set descriptor heap
			ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvHeap.Get() };
			cmdList->SetDescriptorHeaps((UINT)ArraySize(descriptorHeaps), descriptorHeaps);

            // Clear swapchain and depth buffer. NOTE: This will clear the entire render target view, not just the specified view.
            cmdList->ClearRenderTargetView(renderTargetView, static_cast<const FLOAT*>(m_clearColor.data()), 0, nullptr);
            cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { renderTargetView };
            cmdList->OMSetRenderTargets((UINT)ArraySize(renderTargets), renderTargets, true, &depthStencilView);

            const XMMATRIX spaceToView = XMMatrixInverse(nullptr, LoadXrPose(layerView.pose));
            XrMatrix4x4f projectionMatrix;
            XrMatrix4x4f_CreateProjectionFov(&projectionMatrix, GRAPHICS_D3D, layerView.fov, 0.05f, 100.0f);

            // Set shaders and constant buffers.
            ID3D12Resource* viewProjectionCBuffer = swapchainContext.GetViewProjectionCBuffer();
            ViewProjectionConstantBuffer viewProjection;
            XMStoreFloat4x4(&viewProjection.ViewProjection, XMMatrixTranspose(spaceToView * LoadXrMatrix(projectionMatrix)));
            {
                void* data;
                const D3D12_RANGE readRange{ 0, 0 };
                CHECK_HRCMD(viewProjectionCBuffer->Map(0, &readRange, &data));
                memcpy(data, &viewProjection, sizeof(viewProjection));
                viewProjectionCBuffer->Unmap(0, nullptr);
            }

            cmdList->SetGraphicsRootConstantBufferView(1, viewProjectionCBuffer->GetGPUVirtualAddress());

            // Bind texture
			cmdList->SetGraphicsRootDescriptorTable(2, m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

            // Set cube primitive data.
            {
                const D3D12_VERTEX_BUFFER_VIEW vertexBufferView[] = {{m_cubeVertexBuffer->GetGPUVirtualAddress(), sizeof(Geometry::c_cubeVertices), sizeof(Geometry::Vertex)}};
                cmdList->IASetVertexBuffers(0, (UINT)ArraySize(vertexBufferView), vertexBufferView);

                D3D12_INDEX_BUFFER_VIEW indexBufferView{ m_cubeIndexBuffer->GetGPUVirtualAddress(), sizeof(Geometry::c_cubeIndices), DXGI_FORMAT_R16_UINT };
                cmdList->IASetIndexBuffer(&indexBufferView);

                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            constexpr uint32_t cubeCBufferSize = AlignTo<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(sizeof(ModelConstantBuffer));
            swapchainContext.RequestModelCBuffer(static_cast<uint32_t>(cubeCBufferSize * (cubes.size() + 1)));
            ID3D12Resource* modelCBuffer = swapchainContext.GetModelCBuffer();

            // Render each cube
            uint32_t offset = 0;
            for (const Cube& cube : cubes) {
                // Compute and update the model transform.
                ModelConstantBuffer model;
                XMStoreFloat4x4(&model.Model, XMMatrixTranspose(XMMatrixScaling(cube.Scale.x, cube.Scale.y, cube.Scale.z) * LoadXrPose(cube.Pose)));
                {
                    uint8_t* data;
                    const D3D12_RANGE readRange{ 0, 0 };
                    CHECK_HRCMD(modelCBuffer->Map(0, &readRange, reinterpret_cast<void**>(&data)));
                    memcpy(data + offset, &model, sizeof(model));
                    const D3D12_RANGE writeRange{ offset, offset + cubeCBufferSize };
                    modelCBuffer->Unmap(0, &writeRange);
                }

                cmdList->SetGraphicsRootConstantBufferView(0, modelCBuffer->GetGPUVirtualAddress() + offset);

                // Draw the cube.
                cmdList->DrawIndexedInstanced((UINT)ArraySize(Geometry::c_cubeIndices), 1, 0, 0, 0);

                offset += cubeCBufferSize;
            }

			// Set sphere primitive data.
            {
                const D3D12_VERTEX_BUFFER_VIEW vertexBufferView[] = { {m_sphereVertexBuffer->GetGPUVirtualAddress(), m_sphereVertexCount * sizeof(Geometry::Vertex), sizeof(Geometry::Vertex)}};
                cmdList->IASetVertexBuffers(0, (UINT)ArraySize(vertexBufferView), vertexBufferView);

                D3D12_INDEX_BUFFER_VIEW indexBufferView{ m_sphereIndexBuffer->GetGPUVirtualAddress(), m_sphereIndexCount * sizeof(uint16_t), DXGI_FORMAT_R16_UINT};
                cmdList->IASetIndexBuffer(&indexBufferView);

                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            // Render sphere
            {
                // Compute and update the model transform.
                ModelConstantBuffer model;
                XMStoreFloat4x4(&model.Model, XMMatrixTranspose(XMMatrixScaling(.1f, .1f, .1f)));
                {
                    uint8_t* data;
                    const D3D12_RANGE readRange{ 0, 0 };
                    CHECK_HRCMD(modelCBuffer->Map(0, &readRange, reinterpret_cast<void**>(&data)));
                    memcpy(data + offset, &model, sizeof(model));
                    const D3D12_RANGE writeRange{ offset, offset + cubeCBufferSize };
                    modelCBuffer->Unmap(0, &writeRange);
                }

                cmdList->SetGraphicsRootConstantBufferView(0, modelCBuffer->GetGPUVirtualAddress() + offset);
                cmdList->DrawIndexedInstanced(m_sphereIndexCount, 1, 0, 0, 0);

                offset += cubeCBufferSize;
            }
            
            // Copy result to desktop window preview
            if (!m_previewSwapchainInitialized)
            {
                m_desktopView.InitSwapchain(m_device.Get(), colorTextureDesc.Format, colorTextureDesc.Width, colorTextureDesc.Height);
                m_previewSwapchainInitialized = true;
            }
            m_desktopView.CopyRenderResultToPreview(cmdList.Get(), colorTexture, 0);

            CHECK_HRCMD(cmdList->Close());
            ID3D12CommandList* cmdLists[] = { cmdList.Get() };
            m_cmdQueue->ExecuteCommandLists((UINT)ArraySize(cmdLists), cmdLists);

            SignalFence();
            swapchainContext.SetFrameFenceValue(m_fenceValue);
        }

        void SignalFence() {
            ++m_fenceValue;
            CHECK_HRCMD(m_cmdQueue->Signal(m_fence.Get(), m_fenceValue));
        }

        void CpuWaitForFence(uint64_t fenceValue) {
            if (m_fence->GetCompletedValue() < fenceValue) {
                CHECK_HRCMD(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));
                const uint32_t retVal = WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
                if (retVal != WAIT_OBJECT_0) {
                    CHECK_HRCMD(E_FAIL);
                }
            }
        }

        void WaitForGpu() {
            SignalFence();
            CpuWaitForFence(m_fenceValue);
        }

        void UpdateOptions(const std::shared_ptr<Options>& options) override { m_clearColor = options->GetBackgroundClearColor(); }

    private:
        const ComPtr<ID3DBlob> m_vertexShaderBytes;
        const ComPtr<ID3DBlob> m_pixelShaderBytes;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_cmdQueue;
        ComPtr<ID3D12Fence> m_fence;
        uint64_t m_fenceValue = 0;
        HANDLE m_fenceEvent = INVALID_HANDLE_VALUE;
        std::list<SwapchainImageContext> m_swapchainImageContexts;
        std::map<const XrSwapchainImageBaseHeader*, SwapchainImageContext*> m_swapchainImageContextMap;
        XrGraphicsBindingD3D12KHR m_graphicsBinding{ XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };
        ComPtr<ID3D12RootSignature> m_rootSignature;
        std::map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> m_pipelineStates;
        ComPtr<ID3D12Resource> m_cubeVertexBuffer;
        ComPtr<ID3D12Resource> m_cubeIndexBuffer;
        ComPtr<ID3D12Resource> m_sphereVertexBuffer;
        ComPtr<ID3D12Resource> m_sphereIndexBuffer;
        ComPtr<ID3D12Resource> m_texture;
        UINT m_sphereVertexCount;
        UINT m_sphereIndexCount;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
        std::array<float, 4> m_clearColor;
        
        bool m_previewSwapchainInitialized = false;
        uint32_t m_maxSwapchainImageCount = 0;

        DesktopView m_desktopView{};
    };
}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_D3D12(const std::shared_ptr<Options>& options,
    std::shared_ptr<IPlatformPlugin> platformPlugin) {
    return std::make_shared<D3D12GraphicsPlugin>(options, platformPlugin);
}

#endif