#pragma once

#include "resource.h"
#include "resources/shader_resources.h"
#include "resources/data_formats.h"
#include "Font.h"

//------------------------------------------------------------------------------------------------

using Microsoft::WRL::ComPtr;

void DebugTrace(const wchar_t* fmt, ...);

//------------------------------------------------------------------------------------------------
// Noise functions

// Some large interesting (i.e. with well spread bit patterns) primes for noise generation.
// Several orders of magnitude difference
const int neNoisePrime6SF = 769729;
const int neNoisePrime7SF = 6542989;
const int neNoisePrime8SF = 38370263;
const int neNoisePrime9SF = 198491317;

inline uint32_t NoiseMake2dKey(const uint32_t key1, const uint32_t key2)
{
    constexpr uint32_t PRIME1 = neNoisePrime9SF;  // Large interesting prime
    return key1 + (key2 * PRIME1);
}

inline uint32_t NoiseMake3dKey(const uint32_t key1, const uint32_t key2, const uint32_t key3)
{
    constexpr uint32_t PRIME1 = neNoisePrime9SF;    // Large interesting prime
    constexpr uint32_t PRIME2 = neNoisePrime7SF;    // Another large interesting prime (order of mag different)
    return key1 + (key2 * PRIME1) + (key3 * PRIME2);
}

inline uint32_t NoiseMake4dKey(const uint32_t key1, const uint32_t key2, const uint32_t key3, const uint32_t key4)
{
    constexpr uint32_t PRIME1 = neNoisePrime9SF;    // Large interesting prime
    constexpr uint32_t PRIME2 = neNoisePrime7SF;    // Another large interesting prime (order of mag different)
    constexpr uint32_t PRIME3 = neNoisePrime8SF;    // Another large interesting prime (order of mag different)
    return key1 + (key2 * PRIME1) + (key3 * PRIME2) + (key4 * PRIME3);
}

// [0, UINT_MAX]
inline uint32_t Noise1dUnsigned(const uint32_t position, const uint32_t seed)
{
    constexpr uint32_t BIT_NOISE1 = 0xb5297a4d;
    constexpr uint32_t BIT_NOISE2 = 0x68e31da4;
    constexpr uint32_t BIT_NOISE3 = 0x1b56c4e9;

    uint32_t noise = position;
    noise *= BIT_NOISE1;
    noise += seed;
    noise ^= (noise >> 8);
    noise += BIT_NOISE2;
    noise ^= (noise << 8);
    noise *= BIT_NOISE3;
    noise ^= (noise >> 8);
    return noise;
}

// [INT_MIN, INT_MAX]
inline int32_t Noise1dSigned(const uint32_t position, const uint32_t seed)
{
    return static_cast<int32_t>(Noise1dUnsigned(position, seed));
}

// [0, 1.0f]
inline float Noise1dUnsignedF(const uint32_t position, const uint32_t seed)
{
    return static_cast<float>(Noise1dUnsigned(position, seed)) / 4294967295.0f;
}

// [-1.0f, 1.0f]
inline float Noise1dSignedF(const uint32_t position, const uint32_t seed)
{
    return static_cast<float>(Noise1dSigned(position, seed)) / 2147483647.0f;
}

//------------------------------------------------------------------------------------------------
// Descriptor heaps

class ShaderDescriptorHeap
{
public:
    ShaderDescriptorHeap() : m_descriptorSize(0)
    {}
        
    HRESULT Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
    {
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
        if (FAILED(hr))
            return hr;

        m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Null resource views
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        for (unsigned srv = 0; srv < ShaderShared::getSrvCount(); ++srv)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(nullptr, &srvDesc, handle);
            handle.ptr += m_descriptorSize;
        }

        for (unsigned uav = 0; uav < ShaderShared::getUavCount(); ++uav)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 1;
            uavDesc.Buffer.StructureByteStride = 4;
            device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, handle);
            handle.ptr += m_descriptorSize;
        }

        return hr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle( ShaderShared::CBV_TYPE cbv, unsigned index = 0 )
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getCbvOffset( cbv, index );
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle( ShaderShared::CBV_TYPE cbv, unsigned index = 0 )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getCbvOffset( cbv, index );
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(ShaderShared::SRV_TYPE srv, unsigned index = 0 )
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getSrvOffset(srv, index);
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(ShaderShared::SRV_TYPE srv, unsigned index = 0 )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getSrvOffset(srv, index);
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(ShaderShared::UAV_TYPE uav, unsigned index = 0 )
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getUavOffset(uav, index);
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(ShaderShared::UAV_TYPE uav, unsigned index = 0 )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * ShaderShared::getUavOffset(uav, index);
        return handle;
    }

    ID3D12DescriptorHeap* GetHeap()
    {
        return m_heap.Get();
    }

private:
    ComPtr<ID3D12DescriptorHeap>    m_heap;
    UINT    m_descriptorSize;
};

//------------------------------------------------------------------------------------------------

class RtvDescriptorHeap
{
public:
    RtvDescriptorHeap() : m_descriptorSize(0)
    {}

    HRESULT Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
    {
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
        if (FAILED(hr))
            return hr;

        m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        return hr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(const int offset)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += m_descriptorSize * offset;
        return handle;
    }

private:
    ComPtr<ID3D12DescriptorHeap>    m_heap;
    UINT    m_descriptorSize;
};

//------------------------------------------------------------------------------------------------

// Does statistical analysis of stat counter.
class StatCounter
{
public:
    StatCounter() : m_average( 0.0f ), m_variance( 0.0f )
    {
    }

    void reset( float initial )
    {
        m_average = initial;
        m_variance = 0.0f;
    }

    // Exponential moving average
    void addData( float v )
    {
        const float alpha = 0.04f;    // ~25 frames.
        float delta = v - m_average;

        m_average += delta * alpha;
        m_variance = (1.0f - alpha) * (m_variance + alpha * delta*delta);
    }

    float getAverage() const
    {
        return m_average;
    }
    float getStdDeviation() const
    {
        return sqrt( m_variance );
    }

private:
    float    m_average;
    float    m_variance;
};

//------------------------------------------------------------------------------------------------
// Rocket Sim class - main simulation framework.

class RocketSim
{
public:
    RocketSim() : 
        m_hwnd(nullptr),
        m_noiseStep(0),
        m_frameIndex(0),
        m_lastFrame(0.0f),
        m_frameTime(0.0f),
        m_viewport(),
        m_scissorRect(),
        m_currentViewport(&m_viewport),
        m_fenceValues{},
        m_selectedData(90),
        m_autoSelectData(true)
    {
        LARGE_INTEGER tf;
        QueryPerformanceFrequency(&tf);
        // deliberate use of double
        m_timerFrequency = float(tf.QuadPart / 1000.0);

        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&m_timerBase));
    }

    enum ShaderTypes
    {
        ShaderType_Vertex,
        ShaderType_Pixel,
        ShaderType_Domain,
        ShaderType_Hull,
        ShaderType_Geometry,
        ShaderType_Compute,
        ShaderType_Count
    };

    static const D3D12_RENDER_TARGET_BLEND_DESC s_opaqueRenderTargetBlendDesc;
    static const D3D12_RENDER_TARGET_BLEND_DESC s_blendedRenderTargetBlendDesc;
    
    void    InitRenderer(HWND hwnd);
    void    LoadBaseResources();

    void    RenderFrame();
    void    Shutdown();

    float   xPositionToDataIndex( int x );
    float   yPositionToDataIndex( int y );

    void    MouseClicked( int x, int y, bool shiftPressed, bool controlPressed );
    void    MouseReleased( int x, int y, bool captureLost );
    void    RMouseClicked( int x, int y, bool shiftPressed, bool controlPressed );

    ID3D12Device*               GetDevice() { return m_device.Get(); }
    ID3D12GraphicsCommandList*  GetCommandList() { return m_commandList.Get(); }
    
    const D3D12_VIEWPORT&   GetViewport() const { return *m_currentViewport; }
    uint32_t                GetFrameIndex() const { return m_frameIndex; }

    float                   GetTimestampMilliSec() const
    {
        LARGE_INTEGER ts;
        QueryPerformanceCounter(&ts);
        return (ts.QuadPart - m_timerBase) / m_timerFrequency;
    }

    ComPtr<ID3D12Resource>  CreateUploadBuffer(const wchar_t* name, const void* data, size_t size);
    ComPtr<ID3D12Resource>  CreateReadbackBuffer( const wchar_t* name, size_t size );
    ComPtr<ID3D12Resource>  CreateDefaultBuffer(const wchar_t* name, const void* data, size_t size, D3D12_RESOURCE_STATES targetState, bool allowUAV);
    ComPtr<ID3D12Resource>  CreateTexture(const wchar_t* name, const void* data, size_t width, size_t height, DXGI_FORMAT format, size_t texelSize, D3D12_RESOURCE_STATES targetState, bool allowUAV);
    ID3D12PipelineState*    CreateGraphicsPSO(const wchar_t* name, const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT numInputElements,
                                              const std::array<ComPtr<ID3DBlob>, ShaderType_Count>& shaderList,
                                              D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK,
                                              const D3D12_RENDER_TARGET_BLEND_DESC& blendDesc = s_opaqueRenderTargetBlendDesc );
    ID3D12PipelineState*    CreateComputePSO(const wchar_t* name, ID3DBlob* shader);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(ShaderShared::SRV_TYPE srv)
    {
        return m_shaderHeap.GetCPUHandle(srv);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(ShaderShared::SRV_TYPE srv)
    {
        return m_shaderHeap.GetGPUHandle(srv);
    }

private:
    enum class ErrorResult
    {
        ABORT,
        CONTINUE,
        RETRY
    };

    enum GraphList
    {
        GraphList_Height,
        GraphList_Velocity,
        GraphList_Q,
        GraphList_Mass,
        GraphList_GuidancePitch,
        GraphList_Pitch,
        GraphList_Apoapsis,
        GraphList_Eccentricity,
        GraphList_TWR,
        GraphList_AoA,
        GraphList_Position,

        GraphList_Count
    };

	enum HeatmapList
	{
		HeatmapList_MaxQ,
        HeatmapList_MaxMass,
        HeatmapList_OrbitVariance,
        HeatmapList_Combined,

		HeatmapList_Count
	};

    enum DispatchList
    {
        DispatchList_SimulateFlight,
        DispatchList_FlightDataExtents,

        DispatchList_Count
    };

    const int32_t c_xHeatmapSpacing = 2;
    const int32_t c_yHeatmapSpacing = 20;

    struct PendingUploadData
    {
        ComPtr<ID3D12Resource>  source;
        ComPtr<ID3D12Resource>  dest;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT  layout;
        D3D12_RESOURCE_STATES   targetState;
    };

    struct PendingDeleteData
    {
        ComPtr<ID3D12Pageable>  resource;
        UINT64                  fence;

        bool operator==(const ID3D12Pageable* r) const
        {
            return r == resource.Get();
        }
    };

    struct TrackedFile
    {
        const wchar_t   filename[MAX_PATH];
        FILETIME        lastUpdate;
        void (RocketSim::* updateCallback)(TrackedFile&);
        void*           userData;
        std::vector<std::wstring>   errorList;
    };

    struct DrawData
    {
        const wchar_t*      name = nullptr;
        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3DBlob>    vertexShader;
        ComPtr<ID3DBlob>    pixelShader;
    };

    struct GraphData
    {
        const wchar_t*      name = nullptr;
        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3DBlob>    vertexShader;
    };

    struct HeatmapData
    {
        const wchar_t*      name = nullptr;
        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3DBlob>    pixelShader;
    };

    struct DispatchData
    {
        const wchar_t*      name = nullptr;

        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3DBlob>    shader;

        uint32_t    threadGroupCount[3];
        bool        perStep;    // false means per frame.
    };

    struct ResourceData
    {
        ComPtr<ID3D12Resource>      resource;
        D3D12_CPU_DESCRIPTOR_HANDLE desc;
        uint32_t                    size;
    };

    struct TrackedResource
    {
        ComPtr<ID3D12Resource>  resource;
        D3D12_RESOURCE_STATES   state;
    };

    ErrorResult     ErrorHandler(HRESULT hr, const wchar_t* szOperation);
    void            ErrorTrace(TrackedFile& trackedfile, const wchar_t* fmt, ...);

    void            UpdateTrackedFiles();
    void            InvalidateTrackedFile( const wchar_t* filename );
    void            DrawTrackedFileErrors();

    bool            CompileShader(const wchar_t* filename, std::array<ID3DBlob**, ShaderType_Count> shaderList, std::vector<std::wstring>& errorList);

    void            ReloadAllGraphShaders( TrackedFile& trackedFile );
    void            ReloadGraphShader(TrackedFile& trackedFile);

    void            ReloadAllHeatmapShaders( TrackedFile & trackedFile );
    void            ReloadHeatmapShader( TrackedFile & trackedFile );

    void            ReloadDrawShader( TrackedFile & trackedFile );
    void            ReloadDispatchShader(TrackedFile& trackedFile);

    void            CreateConstantBuffer( const wchar_t* name, ResourceData& resourceData, const void* data, uint32_t size );
    void            CreateFloat4Buffer( const wchar_t * name, ComPtr<ID3D12Resource>& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, const ShaderShared::float4* data, uint32_t elements );
    void            CreateStructuredBuffer( const wchar_t * name, ComPtr<ID3D12Resource>& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, const void * data, uint32_t stride, uint32_t elements );
    void            CreateRWStructuredBuffer( const wchar_t * name, TrackedResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, D3D12_CPU_DESCRIPTOR_HANDLE uavDescHandle, uint32_t stride, uint32_t elements );

    nlohmann::json  LoadJsonFile( const wchar_t* filename );

    void            ReloadAscentParams( TrackedFile& trackedFile );
    void            ReloadMissionParams( TrackedFile& trackedFile );
    void            ReloadEnvironmentalParams( TrackedFile& trackedFile );
    void            ReloadMachSweep( TrackedFile & trackedFile );
    void            ReloadHermiteCurve( TrackedFile& trackedFile );
    void            ReloadGraphColours( TrackedFile & trackedFile );

    void            WaitForGPU();
    void            BarrierTransition( ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter );
    void            BarrierTransition( TrackedResource& resource, D3D12_RESOURCE_STATES newState );
    void            BarrierTransition( std::vector<TrackedResource*> resourceList, D3D12_RESOURCE_STATES newState );

    void            BarrierUAV( std::vector<TrackedResource*> resourceList );

    void            SetDrawConstants( const ShaderShared::DrawConstData& drawConsts );
    void            SetDispatchConstants( const ShaderShared::DispatchConstData & dispatchConsts );

    void            SetViewport( const D3D12_VIEWPORT& vp ) { m_currentViewport = &vp; m_commandList->RSSetViewports(1, &vp); }

    // Renderer data
    HWND            m_hwnd;
    D3D12_VIEWPORT  m_viewport;
    D3D12_RECT      m_scissorRect;

	D3D12_VIEWPORT  m_graphViewport;
	D3D12_VIEWPORT  m_heatmapViewport;

    const D3D12_VIEWPORT*   m_currentViewport;

    uint64_t        m_timerBase;
    float           m_timerFrequency;
    uint32_t        m_noiseStep;

    float           m_lastFrame;
    float           m_frameTime;

    ComPtr<IDXGISwapChain3>     m_swapChain;
    ComPtr<ID3D12Device>        m_device;
    ComPtr<ID3D12Resource>      m_renderTargets[2];
    ComPtr<ID3D12CommandAllocator>  m_commandAllocators[2];
    ComPtr<ID3D12CommandQueue>      m_commandQueue;
    ComPtr<ID3D12RootSignature>     m_rootSignature;
    ComPtr<ID3D12GraphicsCommandList>   m_commandList;
#if defined(_DEBUG)
    ComPtr<IDXGraphicsAnalysis> m_graphicsAnalysis;
#endif
    ComPtr<ID3D12QueryHeap>     m_queryHeap;
    ComPtr<ID3D12Resource>      m_queryRB;
    StatCounter                 m_simGpuTime;
    uint32_t                    m_tweakTimer;

    RtvDescriptorHeap           m_rtvHeap;
    ShaderDescriptorHeap        m_shaderHeap;

    // Resources
    std::vector<PendingUploadData>    m_pendingUploads;
    std::vector<PendingDeleteData>    m_pendingDeletes;

    ComPtr<ID3D12Resource>          m_frameConstantBuffer;
    ShaderShared::FrameConstData*   m_frameConstData[2];

    ShaderShared::DrawConstData     m_currentDrawConsts;
    ShaderShared::DispatchConstData m_currentDispatchConsts;

    Font                        m_font;

    // Simulation data
    TrackedResource             m_telemetryData;
    TrackedResource             m_debugTelemetryData;
    TrackedResource             m_flightData;
    ComPtr<ID3D12Resource>      m_flightDataRB[2];

    // Loaded data
    ResourceData                m_ascentParamsBuffer;
    ResourceData                m_missionParamsBuffer;
    ResourceData                m_enviroParamsBuffer;
    ResourceData                m_liftMachCurve[4];
    ResourceData                m_dragMachCurve[4];
    ResourceData                m_pressureHeightCurve;
    ResourceData                m_temperatureHeightCurve;
    ResourceData                m_graphColourBuffer;

    std::array<std::array<ShaderShared::AscentParams, 16>, 32>  m_ascentParams;
    ShaderShared::MissionParams m_missionParams;

    float                       m_simulationStepSize;   // In seconds
    uint32_t                    m_telemetryStepSize;    // In sim steps
    uint32_t                    m_telemetryMaxSamples;
    uint32_t                    m_simulationThreadWidth;
    uint32_t                    m_simulationThreadHeight;
    uint32_t                    m_simulationThreadCount;
    uint32_t                    m_simulationStep;
    uint32_t                    m_simStepsPerFrame;

    uint32_t                    m_selectedData;
    bool                        m_autoSelectData;

    POINT                       m_mouseDragStart;
    POINT                       m_mouseDragEnd;
    BOOL                        m_mouseCapture;

    std::array<ComPtr<ID3DBlob>, ShaderType_Count>  m_graphShaders;
    std::array<GraphData, GraphList_Count>          m_graphList;

    std::array<ComPtr<ID3DBlob>, ShaderType_Count>  m_heatmapShaders;
	std::array<HeatmapData, HeatmapList_Count>      m_heatmapList;

    std::array<DispatchData, DispatchList_Count>    m_dispatchList;

    std::vector<GraphList>          m_graphLayout;
	std::vector<HeatmapList>        m_heatmapLayout;
    std::vector<uint32_t>           m_graphColours;

    GraphData                       m_selectionMarker;
    DrawData                        m_graphBackground;
    DrawData                        m_boxDraw;

    std::vector<TrackedResource*>   m_uavResources;

    // Progress bar
    ComPtr<ID3D12PipelineState> m_progressBarPSO;
    ComPtr<ID3D12Resource>      m_progressBarVB[2];
    D3D12_VERTEX_BUFFER_VIEW    m_progressBarVBV[2];

    // Synchronization objects
    UINT        m_frameIndex;
    HANDLE      m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64      m_fenceValues[2];

    // File tracking
    std::vector<TrackedFile>    m_trackedFiles;
};

//------------------------------------------------------------------------------------------------
