// RocketSim.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "RocketSim.h"

#define MAX_LOADSTRING 100

// Global Variables:
static wchar_t szTitle[MAX_LOADSTRING];                  // The title bar text
static wchar_t szWindowClass[MAX_LOADSTRING];            // the main window class name

static RocketSim    s_RocketSim;

//------------------------------------------------------------------------------------------------

static constexpr float  c_RadToDegree = 180.0f / 3.14159265f;
static constexpr float  c_DegreeToRad = 3.14159265f / 180.0f;

static double earthRadius = 6371000;
static double earthMu = 3.986004418e+14;
static double earthg0 = earthMu / (earthRadius * earthRadius);

static constexpr D3D12_RESOURCE_STATES  D3D12_RESOURCE_STATE_SHADER_RESOURCE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

//------------------------------------------------------------------------------------------------

template<typename T>
T sqr( T x )
{
    return x * x;
}

template<typename T>
T cube( T x )
{
    return x * x * x;
}

//------------------------------------------------------------------------------------------------

struct ProgressVertex
{
    float       x, y, z;
    uint32_t    colour;
};

static char barVertexShader[] =
"struct PSInput\n"
"{\n"
"    float4 position : SV_POSITION;\n"
"    float4 color : COLOR;\n"
"};\n"
"\n"
"PSInput VSMain(float4 position : POSITION, float4 colour : COLOR)\n"
"{\n"
"    PSInput result;\n"
"\n"
"    result.position = position;\n"
"    result.color = colour;\n"
"\n"
"    return result;\n"
"}\n"
;

static char barPixelShader[] =
"struct PSInput\n"
"{\n"
"    float4 position : SV_POSITION;\n"
"    float4 color : COLOR;\n"
"};\n"
"\n"
"float4 PSMain(PSInput input) : SV_TARGET\n"
"{\n"
"    return input.color;\n"
"}\n"
;

//------------------------------------------------------------------------------------------------

const D3D12_RENDER_TARGET_BLEND_DESC RocketSim::s_opaqueRenderTargetBlendDesc =
{
    FALSE, FALSE,
    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
    D3D12_LOGIC_OP_NOOP,
    D3D12_COLOR_WRITE_ENABLE_ALL,
};

const D3D12_RENDER_TARGET_BLEND_DESC RocketSim::s_blendedRenderTargetBlendDesc =
{
    TRUE, FALSE,
    D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
    D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
    D3D12_LOGIC_OP_NOOP,
    D3D12_COLOR_WRITE_ENABLE_ALL,
};

//------------------------------------------------------------------------------------------------

void DebugTrace( const wchar_t* fmt, ... )
{
    wchar_t message[2000];

    va_list args;
    va_start( args, fmt );

    if ( vswprintf_s( message, fmt, args ) > 0 )
    {
        wcscat_s( message, L"\r\n" );
        OutputDebugString( message );
    }

    va_end( args );
}

//------------------------------------------------------------------------------------------------

template <typename T>
T RoundUpPow2( T x, T p )
{
    return (x + p - 1) & ~(p - 1);
}

//------------------------------------------------------------------------------------------------

class PixMarker
{
public:
    PixMarker( ID3D12GraphicsCommandList* cmdList, wchar_t const * name ) : m_cmdList( cmdList )
    {
        PIXBeginEvent( m_cmdList, PIX_COLOR_INDEX( s_index++ ), name );
    }

    ~PixMarker()
    {
        PIXEndEvent( m_cmdList );
    }

    static void reset()
    {
        s_index = 0;
    }

private:
    ID3D12GraphicsCommandList * m_cmdList;

    static uint8_t     s_index;
};
uint8_t PixMarker::s_index;

//------------------------------------------------------------------------------------------------

void RocketSim::InitRenderer( HWND hwnd )
{
    m_hwnd = hwnd;

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    {
        ComPtr<ID3D12Debug> debugController;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    if ( ErrorHandler( CreateDXGIFactory1( IID_PPV_ARGS( &factory ) ), L"CreateDXGIFactory" ) == ErrorResult::ABORT )
        return;

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    HRESULT createResult = DXGI_ERROR_NOT_FOUND;

    for ( UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1( adapterIndex, &hardwareAdapter ); ++adapterIndex )
    {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1( &desc );

        if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
        {
            // Don't select the Basic Render Driver adapter.
            hardwareAdapter = nullptr;
            continue;
        }

        createResult = D3D12CreateDevice( hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) );

        if ( SUCCEEDED( createResult ) )
        {
            break;
        }
    }

    if ( ErrorHandler( createResult, L"D3D12CreateDevice" ) == ErrorResult::ABORT )
        return;

    ComPtr<ID3D12InfoQueue> infoQueue;
    m_device.As( &infoQueue );
    if ( infoQueue.Get() )
    {
        // Just map/unmap null ranges enabled by default (these are mobile optimisation warnings).
        std::array<D3D12_MESSAGE_ID, 2> ids =
        {
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
        };

        D3D12_INFO_QUEUE_FILTER filter;
        memset( &filter, 0, sizeof(filter) );
        filter.DenyList.NumIDs = UINT( ids.size() );
        filter.DenyList.pIDList = ids.data();
        infoQueue->AddStorageFilterEntries( &filter );
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if ( ErrorHandler( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ), L"CreateCommandQueue" ) == ErrorResult::ABORT )
        return;

    RECT rc;
    GetClientRect( m_hwnd, &rc );

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = ARRAY_SIZE( m_renderTargets );
    swapChainDesc.Width = rc.right - rc.left;
    swapChainDesc.Height = rc.bottom - rc.top;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    if ( ErrorHandler( factory->CreateSwapChainForHwnd( m_commandQueue.Get(), m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain ), L"CreateSwapChain" ) == ErrorResult::ABORT )
        return;

    // Default viewport and scissor regions
    m_viewport.Width = static_cast<float>(rc.right - rc.left);
    m_viewport.Height = static_cast<float>(rc.bottom - rc.top);
    m_viewport.MaxDepth = 1.0f;

    m_scissorRect.right = static_cast<LONG>(rc.right - rc.left);
    m_scissorRect.bottom = static_cast<LONG>(rc.bottom - rc.top);

    // Disable fullscreen transitions.
    if ( ErrorHandler( factory->MakeWindowAssociation( m_hwnd, DXGI_MWA_NO_ALT_ENTER ), L"MakeWindowAssociation" ) == ErrorResult::ABORT )
        return;

    if ( ErrorHandler( swapChain.As( &m_swapChain ), L"GetSwapChain3" ) == ErrorResult::ABORT )
        return;

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = ARRAY_SIZE( m_renderTargets );
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( ErrorHandler( m_rtvHeap.Create( m_device.Get(), rtvHeapDesc ), L"CreateDescriptorHeap" ) == ErrorResult::ABORT )
            return;

        // Describe and create a shader resource view (SRV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = ShaderShared::getDescriptorCount();
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( ErrorHandler( m_shaderHeap.Create( m_device.Get(), srvHeapDesc ), L"CreateDescriptorHeap" ) == ErrorResult::ABORT )
            return;
    }

    // Create frame resources.
    {
        // Create a RTV for each frame.
        for ( UINT n = 0; n < ARRAY_SIZE( m_renderTargets ); n++ )
        {
            if ( ErrorHandler( m_swapChain->GetBuffer( n, IID_PPV_ARGS( &m_renderTargets[n] ) ), L"GetSwapChainBuffer" ) == ErrorResult::ABORT )
                return;

            m_device->CreateRenderTargetView( m_renderTargets[n].Get(), nullptr, m_rtvHeap.GetCPUHandle( n ) );

            if ( ErrorHandler( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocators[n] ) ), L"CreateCommandAllocator" ) == ErrorResult::ABORT )
                return;
        }
    }

    {
        D3D12_QUERY_HEAP_DESC queryDesc;
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = 2;
        queryDesc.NodeMask = 0;
        m_device->CreateQueryHeap( &queryDesc, IID_PPV_ARGS( &m_queryHeap ) );

        m_queryRB = CreateReadbackBuffer( L"QueryRB", sizeof( uint64_t ) * queryDesc.Count );
    }

    m_simStepsPerFrame = 50;
}

//------------------------------------------------------------------------------------------------

void RocketSim::LoadBaseResources()
{
    // Create a root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if ( FAILED( m_device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof( featureData ) ) ) )
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        D3D12_DESCRIPTOR_RANGE1 ranges[] =
        {
            { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, ShaderShared::getCbvCount(), 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ShaderShared::getSrvCount(), 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
            { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, ShaderShared::getUavCount(), 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
        };

        std::array<D3D12_ROOT_PARAMETER1, 3> rootParameters;
        uint32_t paramIdx = 0;

        rootParameters[paramIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[paramIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[paramIdx].Constants.ShaderRegister = 0;
        rootParameters[paramIdx].Constants.RegisterSpace = 1;
        rootParameters[paramIdx].Constants.Num32BitValues = sizeof( ShaderShared::DrawConstData ) / sizeof( uint32_t );
        ++paramIdx;

        rootParameters[paramIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[paramIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[paramIdx].Descriptor.ShaderRegister = 1;
        rootParameters[paramIdx].Descriptor.RegisterSpace = 1;
        rootParameters[paramIdx].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        ++paramIdx;

        rootParameters[paramIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[paramIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[paramIdx].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE( ranges );
        rootParameters[paramIdx].DescriptorTable.pDescriptorRanges = ranges;
        ++paramIdx;

        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        for ( D3D12_STATIC_SAMPLER_DESC& sampler : samplers )
        {
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler.MipLODBias = 0;
            sampler.MaxAnisotropy = 0;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            sampler.MinLOD = 0.0f;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = UINT( &sampler - samplers );
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Version = featureData.HighestVersion;
        rootSignatureDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParameters.size());
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters.data();
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = ARRAY_SIZE( samplers );
        rootSignatureDesc.Desc_1_1.pStaticSamplers = samplers;
        rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if ( ErrorHandler( D3D12SerializeVersionedRootSignature( &rootSignatureDesc, &signature, &error ), L"SerializeRootSignature" ) == ErrorResult::ABORT )
            return;
        if ( ErrorHandler( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ), L"CreateRootSignature" ) == ErrorResult::ABORT )
            return;
    }

    // Setup resources
    m_ascentParamsBuffer.desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvAscentParams );
    m_missionParamsBuffer.desc = m_shaderHeap.GetCPUHandle( ShaderShared::cbvMissionParams );
    m_enviroParamsBuffer.desc = m_shaderHeap.GetCPUHandle( ShaderShared::cbvEnviroParams );
    m_pressureHeightCurve.desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvPressureHeightCurve );
    m_temperatureHeightCurve.desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvTemperatureHeightCurve );
    m_graphColourBuffer.desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvGraphColours );

    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_liftMachCurve[i].desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvLiftMachCurve, i );
        m_dragMachCurve[i].desc = m_shaderHeap.GetCPUHandle( ShaderShared::srvDragMachCurve, i );
    }

    SetCurrentDirectoryW( L"resources" );

    m_trackedFiles.emplace_back( TrackedFile{ L"graph_draw_pxl.hlsl", { 0 }, &RocketSim::ReloadAllGraphShaders, nullptr } );
    m_trackedFiles.emplace_back( TrackedFile{ L"heatmap_draw_vtx.hlsl",{ 0 }, &RocketSim::ReloadAllHeatmapShaders, nullptr } );

    m_trackedFiles.emplace_back( TrackedFile{ L"simulate_flight_cs.hlsl", { 0 }, &RocketSim::ReloadDispatchShader, &m_dispatchList[DispatchList_SimulateFlight] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"flight_data_extents_cs.hlsl",{ 0 }, &RocketSim::ReloadDispatchShader, &m_dispatchList[DispatchList_FlightDataExtents] } );

    m_trackedFiles.emplace_back( TrackedFile{ L"graph_height_vtx.hlsl", { 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Height] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_position_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Position] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_velocity_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Velocity] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_Q_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Q] } );
	m_trackedFiles.emplace_back( TrackedFile{ L"graph_pitch_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Pitch] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_aoa_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_AoA] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_mass_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Mass] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_apoapsis_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Apoapsis] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_eccentricty_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_Eccentricity] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_guidance_pitch_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_GuidancePitch] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_twr_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_graphList[GraphList_TWR] } );

    m_trackedFiles.emplace_back( TrackedFile{ L"heatmap_maxQ_pxl.hlsl",{ 0 }, &RocketSim::ReloadHeatmapShader, &m_heatmapList[HeatmapList_MaxQ] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"heatmap_mass_pxl.hlsl",{ 0 }, &RocketSim::ReloadHeatmapShader, &m_heatmapList[HeatmapList_MaxMass] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"heatmap_orbit_variance_pxl.hlsl",{ 0 }, &RocketSim::ReloadHeatmapShader, &m_heatmapList[HeatmapList_OrbitVariance] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"heatmap_combined_pxl.hlsl",{ 0 }, &RocketSim::ReloadHeatmapShader, &m_heatmapList[HeatmapList_Combined] } );

    m_trackedFiles.emplace_back( TrackedFile{ L"marker_draw_vtx.hlsl",{ 0 }, &RocketSim::ReloadGraphShader, &m_selectionMarker } );

    m_trackedFiles.emplace_back( TrackedFile{ L"textbox_vtx.hlsl",{ 0 }, &RocketSim::ReloadDrawShader, &m_boxDraw } );
    m_trackedFiles.emplace_back( TrackedFile{ L"textbox_pxl.hlsl",{ 0 }, &RocketSim::ReloadDrawShader, &m_boxDraw } );

    m_trackedFiles.emplace_back( TrackedFile{ L"graph_background_vtx.hlsl",{ 0 }, &RocketSim::ReloadDrawShader, &m_graphBackground } );
    m_trackedFiles.emplace_back( TrackedFile{ L"graph_background_pxl.hlsl",{ 0 }, &RocketSim::ReloadDrawShader, &m_graphBackground } );

    m_trackedFiles.emplace_back( TrackedFile{ L"environmental_params.json",{ 0 }, &RocketSim::ReloadEnvironmentalParams, &m_enviroParamsBuffer } );
    m_trackedFiles.emplace_back( TrackedFile{ L"ascent_params.json",{ 0 }, &RocketSim::ReloadAscentParams, &m_ascentParamsBuffer } );
    m_trackedFiles.emplace_back( TrackedFile{ L"mission_params.json",{ 0 }, &RocketSim::ReloadMissionParams, &m_missionParamsBuffer } );

    m_trackedFiles.emplace_back( TrackedFile{ L"machsweep_s1.csv",{ 0 }, &RocketSim::ReloadMachSweep, &m_liftMachCurve[0] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"machsweep_s1.csv",{ 0 }, &RocketSim::ReloadMachSweep, &m_dragMachCurve[0] } );

    m_trackedFiles.emplace_back( TrackedFile{ L"machsweep_s2.csv",{ 0 }, &RocketSim::ReloadMachSweep, &m_liftMachCurve[1] } );
    m_trackedFiles.emplace_back( TrackedFile{ L"machsweep_s2.csv",{ 0 }, &RocketSim::ReloadMachSweep, &m_dragMachCurve[1] } );

    m_trackedFiles.emplace_back( TrackedFile{ L"pressure_height.json",{ 0 }, &RocketSim::ReloadHermiteCurve, &m_pressureHeightCurve } );
    m_trackedFiles.emplace_back( TrackedFile{ L"temperature_height.json",{ 0 }, &RocketSim::ReloadHermiteCurve, &m_temperatureHeightCurve } );

    m_trackedFiles.emplace_back( TrackedFile{ L"graph_colours.json",{ 0 }, &RocketSim::ReloadGraphColours, &m_graphColourBuffer } );

    m_graphLayout.emplace_back( GraphList_Height );
    m_graphLayout.emplace_back( GraphList_Velocity );
    m_graphLayout.emplace_back( GraphList_Q );
    m_graphLayout.emplace_back( GraphList_GuidancePitch );
    m_graphLayout.emplace_back( GraphList_Pitch );
    m_graphLayout.emplace_back( GraphList_Mass );
    m_graphLayout.emplace_back( GraphList_Apoapsis );
    m_graphLayout.emplace_back( GraphList_Eccentricity );
    m_graphLayout.emplace_back( GraphList_TWR );

    m_heatmapLayout.emplace_back( HeatmapList_MaxMass );
    m_heatmapLayout.emplace_back( HeatmapList_MaxQ );
    m_heatmapLayout.emplace_back( HeatmapList_OrbitVariance );
    m_heatmapLayout.emplace_back( HeatmapList_Combined );

    // Create the command list.
    if ( ErrorHandler( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS( &m_commandList ) ), L"CreateCommandList" ) == ErrorResult::ABORT )
        return;

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    m_commandList->Close();

    // Create progress bar resources
    {
        std::array<ComPtr<ID3DBlob>, ShaderType_Count> shaderList;

        D3DCompile( barVertexShader, sizeof( barVertexShader ), "embedded", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &shaderList[ShaderType_Vertex], nullptr );
        D3DCompile( barPixelShader, sizeof( barPixelShader ), "embedded", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &shaderList[ShaderType_Pixel], nullptr );

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        m_progressBarPSO = CreateGraphicsPSO( L"ProgressBarPSO", inputElementDescs, ARRAY_SIZE( inputElementDescs ), shaderList );

        for ( UINT i = 0; i < ARRAY_SIZE( m_progressBarVB ); ++i )
        {
            m_progressBarVB[i] = CreateUploadBuffer( L"ProgressBarVB", nullptr, sizeof( ProgressVertex ) * 6 );

            m_progressBarVBV[i].BufferLocation = m_progressBarVB[i]->GetGPUVirtualAddress();
            m_progressBarVBV[i].StrideInBytes = sizeof( ProgressVertex );
            m_progressBarVBV[i].SizeInBytes = sizeof( ProgressVertex ) * 6;
        }
    }

    // Create simulation resources
    {
        m_simulationStepSize = 1.0f / 50.0f;
        m_telemetryStepSize = 5;
        m_telemetryMaxSamples = uint32_t( 600.0f / (m_simulationStepSize * m_telemetryStepSize) + 0.5f );
        m_simulationThreadWidth = 16;
        m_simulationThreadHeight = 32;
        m_simulationThreadCount = 16 * 32;

        CreateRWStructuredBuffer( L"TelemetryData", m_telemetryData, m_shaderHeap.GetCPUHandle( ShaderShared::srvTelemetryData ), m_shaderHeap.GetCPUHandle( ShaderShared::uavTelemetryData ),
                                  sizeof( ShaderShared::TelemetryData ), m_telemetryMaxSamples * m_simulationThreadCount );

        CreateRWStructuredBuffer( L"FlightData", m_flightData, m_shaderHeap.GetCPUHandle( ShaderShared::srvFlightData ), m_shaderHeap.GetCPUHandle( ShaderShared::uavFlightData ),
                                  sizeof( ShaderShared::FlightData ), (m_simulationThreadCount + 2) );

        m_flightDataRB[0] = CreateReadbackBuffer( L"FlightDataRB[0]", sizeof( ShaderShared::FlightData ) * (m_simulationThreadCount + 2) );
        m_flightDataRB[1] = CreateReadbackBuffer( L"FlightDataRB[1]", sizeof( ShaderShared::FlightData ) * (m_simulationThreadCount + 2) );

        m_dispatchList[DispatchList_SimulateFlight].name = L"SimulateFlight";
        m_dispatchList[DispatchList_SimulateFlight].threadGroupCount[0] = 1;
        m_dispatchList[DispatchList_SimulateFlight].threadGroupCount[1] = 1;
        m_dispatchList[DispatchList_SimulateFlight].threadGroupCount[2] = 1;
        m_dispatchList[DispatchList_SimulateFlight].perStep = true;

        m_dispatchList[DispatchList_FlightDataExtents].name = L"FlightDataExtents";
        m_dispatchList[DispatchList_FlightDataExtents].threadGroupCount[0] = 1;
        m_dispatchList[DispatchList_FlightDataExtents].threadGroupCount[1] = 1;
        m_dispatchList[DispatchList_FlightDataExtents].threadGroupCount[2] = 1;
        m_dispatchList[DispatchList_FlightDataExtents].perStep = false;

        m_simulationStep = 0;
    }

    m_frameConstantBuffer = CreateUploadBuffer( L"Frame Constant Buffer", nullptr, 2 * RoundUpPow2( sizeof( ShaderShared::FrameConstData ), 256llu ) );

    D3D12_RANGE readRange = { 0, 0 };
    m_frameConstantBuffer->Map( 0, &readRange, (void**)&m_frameConstData[0] );
    m_frameConstData[1] = (ShaderShared::FrameConstData*)((uintptr_t)m_frameConstData[0] + RoundUpPow2( sizeof( ShaderShared::FrameConstData ), 256llu ));

    m_font.Create( m_hwnd, this, 48 );

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        if ( ErrorHandler( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ), L"CreateFence" ) == ErrorResult::ABORT )
            return;
        ++m_fenceValues[m_frameIndex];

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
        if ( m_fenceEvent == nullptr )
        {
            if ( ErrorHandler( HRESULT_FROM_WIN32( GetLastError() ), L"CreateEvent" ) == ErrorResult::ABORT )
                return;
        }

        WaitForGPU();
    }

#if defined(_DEBUG)
    DXGIGetDebugInterface1( 0, IID_PPV_ARGS( &m_graphicsAnalysis ) );
#endif
}

//------------------------------------------------------------------------------------------------

bool RocketSim::CompileShader( const wchar_t* filename, std::array<ID3DBlob**, ShaderType_Count> shaderList, std::vector<std::wstring>& errorList )
{
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ComPtr<ID3DBlob> errors;

    char target[] = "xs_5_1";

    const wchar_t* targetTag = wcsrchr( filename, L'_' );

    if ( targetTag && isalpha( targetTag[1] ) )
    {
        target[0] = char( targetTag[1] );
    }

    const char shaderTypes[ShaderType_Count] = { 'v', 'p', 'd', 'h', 'g', 'c' };
    ptrdiff_t shader = std::find( shaderTypes, shaderTypes + ARRAY_SIZE( shaderTypes ), target[0] ) - shaderTypes;

    if ( shader == ARRAY_SIZE( shaderTypes ) )
    {
        DebugTrace( L"Unable to deduce shader type for file: %s", filename );
        return false;
    }

    if ( !shaderList[shader] )
    {
        DebugTrace( L"Cannot load shader in this context: %s", filename );
        return false;
    }

    if ( *shaderList[shader] )
    {
        (*shaderList[shader])->Release();
        *shaderList[shader] = nullptr;
    }

    errorList.clear();

    char targetDefine[] = "SHADER_TYPE_XS";
    targetDefine[12] = static_cast<char>(toupper(target[0]));

    D3D_SHADER_MACRO defines[2] = { 0 };
    defines[0].Name = targetDefine;
    defines[0].Definition = "";

    HRESULT hr;
    while ( FAILED( hr = D3DCompileFromFile( filename, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", target, compileFlags, 0, shaderList[shader], &errors ) ) )
    {
        // If it's a sharing violation just have another crack
        if ( hr == HRESULT_FROM_WIN32( ERROR_SHARING_VIOLATION ) )
        {
            continue;
        }

        if ( errors && errors->GetBufferSize() )
        {
            DebugTrace( L"Failed to compile shader: %S", errors->GetBufferPointer() );

            char* context = nullptr;
            char* token = strtok_s( static_cast<char*>(errors->GetBufferPointer()), "\n", &context );

            while ( token )
            {
                size_t len;
                mbstowcs_s( &len, nullptr, 0, token, _TRUNCATE );
                wchar_t* wtoken = static_cast<wchar_t*>(alloca( sizeof( wchar_t ) * len ));
                mbstowcs_s( nullptr, wtoken, len, token, _TRUNCATE );

                errorList.emplace_back( wtoken );

                token = strtok_s( nullptr, "\n", &context );
            }
        }
        else
        {
            DebugTrace( L"Failed to load shader %s: %d", filename, hr );
        }

        break;
    }

    // true even if compile failed - indicates one of the shaders changed, not that it was a valid compile
    return true;
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadAllGraphShaders( TrackedFile& trackedFile )
{
    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;

    for ( int i = 0; i < ShaderType_Count; ++i )
    {
        shaderLoadList[i] = m_graphShaders[i].GetAddressOf();
    }

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        std::array<ComPtr<ID3DBlob>, ShaderType_Count> graphShaders( m_graphShaders );

        for ( GraphData& graphData : m_graphList )
        {
            if ( graphData.pso )
            {
                m_pendingDeletes.emplace_back( PendingDeleteData{ graphData.pso, m_fenceValues[m_frameIndex] } );
                graphData.pso = nullptr;
            }

            if ( graphData.vertexShader )
            {
                graphShaders[ShaderType_Vertex] = graphData.vertexShader;

                graphData.pso = CreateGraphicsPSO( graphData.name, nullptr, 0, graphShaders, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, D3D12_CULL_MODE_NONE, s_blendedRenderTargetBlendDesc );
            }
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadGraphShader( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    GraphData* graphData = static_cast<GraphData*>(trackedFile.userData);

    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;
    shaderLoadList.fill( nullptr );

    shaderLoadList[ShaderType_Vertex] = graphData->vertexShader.GetAddressOf();

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        if ( graphData->pso )
        {
            m_pendingDeletes.emplace_back( PendingDeleteData{ graphData->pso, m_fenceValues[m_frameIndex] } );
            graphData->pso = nullptr;
        }

        std::array<ComPtr<ID3DBlob>, ShaderType_Count> graphShaders( m_graphShaders );
        graphShaders[ShaderType_Vertex] = graphData->vertexShader;

        graphData->name = trackedFile.filename;
        graphData->pso = CreateGraphicsPSO( graphData->name, nullptr, 0, graphShaders, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, D3D12_CULL_MODE_NONE, s_blendedRenderTargetBlendDesc );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadAllHeatmapShaders( TrackedFile& trackedFile )
{
    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;

    for ( int i = 0; i < ShaderType_Count; ++i )
    {
        shaderLoadList[i] = m_heatmapShaders[i].GetAddressOf();
    }

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        std::array<ComPtr<ID3DBlob>, ShaderType_Count> heatmapShaders( m_heatmapShaders );

        for ( HeatmapData& heatmapData : m_heatmapList )
        {
            if ( heatmapData.pso )
            {
                m_pendingDeletes.emplace_back( PendingDeleteData{ heatmapData.pso, m_fenceValues[m_frameIndex] } );
                heatmapData.pso = nullptr;
            }

            if ( heatmapData.pixelShader )
            {
                heatmapShaders[ShaderType_Pixel] = heatmapData.pixelShader;

                heatmapData.pso = CreateGraphicsPSO( heatmapData.name, nullptr, 0, heatmapShaders, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_CULL_MODE_NONE, s_opaqueRenderTargetBlendDesc );
            }
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadHeatmapShader( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    HeatmapData* heatmapData = static_cast<HeatmapData*>(trackedFile.userData);

    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;
    shaderLoadList.fill( nullptr );

    shaderLoadList[ShaderType_Pixel] = heatmapData->pixelShader.GetAddressOf();

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        if ( heatmapData->pso )
        {
            m_pendingDeletes.emplace_back( PendingDeleteData{ heatmapData->pso, m_fenceValues[m_frameIndex] } );
            heatmapData->pso = nullptr;
        }

        std::array<ComPtr<ID3DBlob>, ShaderType_Count> heatmapShaders( m_heatmapShaders );
        heatmapShaders[ShaderType_Pixel] = heatmapData->pixelShader;

        heatmapData->name = trackedFile.filename;
        heatmapData->pso = CreateGraphicsPSO( heatmapData->name, nullptr, 0, heatmapShaders, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_CULL_MODE_NONE, s_opaqueRenderTargetBlendDesc );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadDrawShader( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    DrawData* drawData = static_cast<DrawData*>(trackedFile.userData);

    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;
    shaderLoadList.fill( nullptr );

    shaderLoadList[ShaderType_Vertex] = drawData->vertexShader.GetAddressOf();
    shaderLoadList[ShaderType_Pixel] = drawData->pixelShader.GetAddressOf();

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        if ( drawData->pso )
        {
            m_pendingDeletes.emplace_back( PendingDeleteData{ drawData->pso, m_fenceValues[m_frameIndex] } );
            drawData->pso = nullptr;
        }

        std::array<ComPtr<ID3DBlob>, ShaderType_Count> drawShaders;
        drawShaders.fill( nullptr );
        drawShaders[ShaderType_Vertex] = drawData->vertexShader;
        drawShaders[ShaderType_Pixel] = drawData->pixelShader;

        drawData->name = trackedFile.filename;
        drawData->pso = CreateGraphicsPSO( drawData->name, nullptr, 0, drawShaders, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_CULL_MODE_NONE, s_blendedRenderTargetBlendDesc );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadDispatchShader( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    DispatchData* dispatchData = static_cast<DispatchData*>(trackedFile.userData);

    std::array<ID3DBlob**, ShaderType_Count> shaderLoadList;
    shaderLoadList.fill( nullptr );

    shaderLoadList[ShaderType_Compute] = dispatchData->shader.GetAddressOf();

    if ( CompileShader( trackedFile.filename, shaderLoadList, trackedFile.errorList ) )
    {
        if ( dispatchData->pso )
        {
            m_pendingDeletes.emplace_back( PendingDeleteData{ dispatchData->pso, m_fenceValues[m_frameIndex] } );
            dispatchData->pso = nullptr;
        }

        dispatchData->pso = CreateComputePSO( dispatchData->name, dispatchData->shader.Get() );
    }

    // Restart sim
    m_simulationStep = 0;
}

//------------------------------------------------------------------------------------------------

void RocketSim::CreateConstantBuffer( const wchar_t* name, ResourceData& resourceData, const void* data, uint32_t size )
{
    if ( resourceData.resource )
    {
        m_pendingDeletes.emplace_back( PendingDeleteData{ resourceData.resource, m_fenceValues[m_frameIndex] } );
    }

    resourceData.resource = CreateDefaultBuffer( name, data, RoundUpPow2( size, 256u ), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, false );

    if ( resourceData.resource )
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = resourceData.resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = RoundUpPow2( size, 256u );
        m_device->CreateConstantBufferView( &cbvDesc, resourceData.desc );
        resourceData.size = size;
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::CreateFloat4Buffer( const wchar_t * name, ComPtr<ID3D12Resource>& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, const ShaderShared::float4* data, uint32_t elements )
{
    if ( resource )
    {
        m_pendingDeletes.emplace_back( PendingDeleteData{ resource, m_fenceValues[m_frameIndex] } );
    }

    resource = CreateDefaultBuffer( name, data, sizeof( ShaderShared::float4 ) * size_t( elements ), D3D12_RESOURCE_STATE_SHADER_RESOURCE, false );

    if ( resource )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = elements;
        m_device->CreateShaderResourceView( resource.Get(), &srvDesc, srvDescHandle );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::CreateStructuredBuffer( const wchar_t* name, ComPtr<ID3D12Resource>& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, const void* data, uint32_t stride, uint32_t elements )
{
    if ( resource )
    {
        m_pendingDeletes.emplace_back( PendingDeleteData{ resource, m_fenceValues[m_frameIndex] } );
    }

    resource = CreateDefaultBuffer( name, data, size_t( stride ) * size_t( elements ), D3D12_RESOURCE_STATE_SHADER_RESOURCE, false );

    if ( resource )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = elements;
        srvDesc.Buffer.StructureByteStride = stride;
        m_device->CreateShaderResourceView( resource.Get(), &srvDesc, srvDescHandle );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::CreateRWStructuredBuffer( const wchar_t* name, TrackedResource& resource, D3D12_CPU_DESCRIPTOR_HANDLE srvDescHandle, D3D12_CPU_DESCRIPTOR_HANDLE uavDescHandle, uint32_t stride, uint32_t elements )
{
    if ( resource.resource )
    {
        m_pendingDeletes.emplace_back( PendingDeleteData{ resource.resource, m_fenceValues[m_frameIndex] } );
    }

    resource.resource = CreateDefaultBuffer( name, nullptr, size_t(stride) * size_t(elements), D3D12_RESOURCE_STATE_COMMON, true );

    if ( resource.resource )
    {
        resource.state = D3D12_RESOURCE_STATE_COMMON;
        m_uavResources.push_back( &resource );

        if ( srvDescHandle.ptr )
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elements;
            srvDesc.Buffer.StructureByteStride = stride;
            m_device->CreateShaderResourceView( resource.resource.Get(), &srvDesc, srvDescHandle );
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = elements;
        uavDesc.Buffer.StructureByteStride = stride;
        m_device->CreateUnorderedAccessView( resource.resource.Get(), nullptr, &uavDesc, uavDescHandle );
    }
}

//------------------------------------------------------------------------------------------------

nlohmann::json RocketSim::LoadJsonFile( const wchar_t* filename )
{
    std::ifstream iStream( filename );
    if ( iStream.fail() )
    {
        throw nlohmann::json::other_error::create( 502, "Failed to open file." );
    }

    nlohmann::json j;
    iStream >> j;

    return j;
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadAscentParams( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    try
    {
        nlohmann::json ascentJson = LoadJsonFile( trackedFile.filename );

        float minSpeed = ascentJson.value( "minSpeed", 20.0f );
        float maxSpeed = ascentJson.value( "maxSpeed", 100.0f );
        float minAngle = ascentJson.value( "minAngle", 1.0f );
        float maxAngle = ascentJson.value( "maxAngle", 5.0f );

        float speedStep = (maxSpeed - minSpeed) / float( m_ascentParams.size() - 1 );
        float angleStep = (maxAngle - minAngle) / float( m_ascentParams[0].size() - 1 );

        for ( uint32_t angle = 0; angle < m_ascentParams[0].size(); ++angle )
        {
            float a = (minAngle + angle * angleStep) * c_DegreeToRad;
            m_ascentParams[0][angle].pitchOverSpeed = minSpeed;
            m_ascentParams[0][angle].sinPitchOverAngle = sin( a );
            m_ascentParams[0][angle].cosPitchOverAngle = cos( a );
            m_ascentParams[0][angle].sinAimAngle = sin( a + c_DegreeToRad );
            m_ascentParams[0][angle].cosAimAngle = cos( a + c_DegreeToRad );
        }

        for ( uint32_t speed = 1; speed < m_ascentParams.size(); ++speed )
        {
            float pitchOverSpeed = minSpeed + speed * speedStep;
            for ( uint32_t angle = 0; angle < m_ascentParams[0].size(); ++angle )
            {
                m_ascentParams[speed][angle].pitchOverSpeed = pitchOverSpeed;
                m_ascentParams[speed][angle].sinPitchOverAngle = m_ascentParams[speed - 1][angle].sinPitchOverAngle;
                m_ascentParams[speed][angle].cosPitchOverAngle = m_ascentParams[speed - 1][angle].cosPitchOverAngle;
                m_ascentParams[speed][angle].sinAimAngle = m_ascentParams[speed - 1][angle].sinAimAngle;
                m_ascentParams[speed][angle].cosAimAngle = m_ascentParams[speed - 1][angle].cosAimAngle;
            }
        }

        ResourceData* resourceData = static_cast<ResourceData*>(trackedFile.userData);
        CreateStructuredBuffer( trackedFile.filename, resourceData->resource, resourceData->desc, m_ascentParams.data(),
                                sizeof( ShaderShared::AscentParams ), static_cast<uint32_t>(m_ascentParams.size() * m_ascentParams[0].size()) );

        // Restart sim
        m_simulationStep = 0;
        m_autoSelectData = true;
    }
    catch ( nlohmann::json::exception& e )
    {
        ErrorTrace( trackedFile, L"%S", e.what() );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadMissionParams( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    try
    {
        nlohmann::json missionParamsJson = LoadJsonFile( trackedFile.filename );

        memset( &m_missionParams, 0, sizeof( m_missionParams ) );

        m_missionParams.stageCount = 0;
        for ( const nlohmann::json& stageJson : missionParamsJson["stages"] )
        {
            m_missionParams.stage[m_missionParams.stageCount].wetMass = stageJson.value( "wetmass", 0.0f );
            m_missionParams.stage[m_missionParams.stageCount].dryMass = stageJson.value( "drymass", 0.0f );
            m_missionParams.stage[m_missionParams.stageCount].IspSL = stageJson.value( "IspSL", 0.0f );
            m_missionParams.stage[m_missionParams.stageCount].IspVac = stageJson.value( "IspVac", 0.0f );
            m_missionParams.stage[m_missionParams.stageCount].rotationRate = c_DegreeToRad;

            float fuelMass = stageJson.value( "fuelmass", 0.0f );
            if ( fuelMass > 0.0f )
                m_missionParams.stage[m_missionParams.stageCount].dryMass = m_missionParams.stage[m_missionParams.stageCount].wetMass - fuelMass;

            if ( m_missionParams.stage[m_missionParams.stageCount].IspVac > 0.0f )
            {
                // Convert thrust (in kN) to mass flow rate
                float thrust = stageJson.value( "thrustVac", 0.0f );
                float thrustLimit = stageJson.value( "thrustLimit", 1.0f );
                thrustLimit = std::min( std::max( thrustLimit, 0.0f ), 1.0f );
                m_missionParams.stage[m_missionParams.stageCount].massFlow = thrust * 1000.0f * thrustLimit / (9.82025f * m_missionParams.stage[m_missionParams.stageCount].IspVac);
            }

            if ( m_missionParams.stage[m_missionParams.stageCount].dryMass > 0.0f )
            {
                if ( ++m_missionParams.stageCount >= ARRAY_SIZE( m_missionParams.stage ) )
                    break;
            }
        }

        double Ap = missionParamsJson.value( "apoapsis", 200000.0 ) + earthRadius;
        double Pe = missionParamsJson.value( "periapsis", 200000.0 ) + earthRadius;

        double a = (Ap + Pe) / 2.0;
        double e = 1 - Pe / a;
        double L = a * (1 - e * e);


        m_missionParams.finalState.r = float(Pe);
        m_missionParams.finalState.rv = 0;
        m_missionParams.finalState.h = float( sqrt( earthMu * L ) );
        m_missionParams.finalState.omega = m_missionParams.finalState.h / float( sqr(Pe) );

        m_missionParams.finalOrbitalEnergy = float( -earthMu / (2 * a));

        CreateConstantBuffer( trackedFile.filename, *static_cast<ResourceData*>(trackedFile.userData), &m_missionParams, sizeof( m_missionParams ) );

        // Restart sim
        m_simulationStep = 0;
        m_autoSelectData = true;
    }
    catch ( nlohmann::json::exception& e )
    {
        ErrorTrace( trackedFile, L"%S", e.what() );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadEnvironmentalParams( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    try
    {
        nlohmann::json enviroParamsJson = LoadJsonFile( trackedFile.filename );

        earthMu = enviroParamsJson.value("gravConstant", earthMu);
        earthRadius = enviroParamsJson.value("earthRadius", earthRadius);
        earthg0 = earthMu / (earthRadius * earthRadius);

        float data[] =
        {
            float( earthMu ),
            float( earthRadius ),
            float( earthg0 ),
            enviroParamsJson.value( "airMolarMass", 0.0289644f ),
            8.3144598f,     // universal gas constant
            enviroParamsJson.value( "adiabaticIndex", 1.4f ),
            enviroParamsJson.value( "launchLatitude", 28.608389f ),
            enviroParamsJson.value( "launchAltitude", 85.0f ),
            enviroParamsJson.value( "rotationPeriod", 86164.098903691f ),
        };

        CreateConstantBuffer( trackedFile.filename, *static_cast<ResourceData*>(trackedFile.userData), data, sizeof( data ) );

        // Reload mission params as they depend on earthRadius
        InvalidateTrackedFile( L"mission_params.json" );

        // Restart sim
        m_simulationStep = 0;
        m_autoSelectData = true;
    }
    catch ( nlohmann::json::exception& e )
    {
        ErrorTrace( trackedFile, L"%S", e.what() );
    }
}

//------------------------------------------------------------------------------------------------

static void GenerateMonotonicInterpolants( std::vector<ShaderShared::float4>& curveData )
{
    std::vector<float> delta;
    for ( uint32_t i = 0; i < curveData.size() - 1; ++i )
    {
        delta.emplace_back( (curveData[i + 1].y - curveData[i].y) / (curveData[i + 1].x - curveData[i].x) );
    }

    std::vector<float> tangents;
    tangents.emplace_back( delta.front() );
    for ( uint32_t i = 1; i < delta.size(); ++i )
    {
        if ( (delta[i - 1] > 0.0f) == (delta[i] > 0.0f) )
            tangents.emplace_back( (delta[i - 1] + delta[i]) / 2 );
        else
            tangents.emplace_back( 0.0f );
    }
    tangents.emplace_back( delta.back() );

    for ( uint32_t i = 0; i < delta.size(); ++i )
    {
        if ( fabs( delta[i] ) < FLT_EPSILON )
        {
            tangents[i] = 0.f;
            tangents[i + 1] = 0.f;
            ++i;
        }
        else
        {
            float alpha = tangents[i] / delta[i];
            float beta = tangents[i + 1] / delta[i];

            if ( alpha < 0.0f || beta < 0.0f )
            {
                tangents[i] = 0.f;
            }
            else
            {
                float s = alpha * alpha + beta * beta;
                if ( s > 9.0f )
                {
                    s = 3.0f / sqrt( s );
                    tangents[i] = s * alpha * delta[i];
                    tangents[i + 1] = s * beta * delta[i];
                }
            }
        }
    }

    for ( uint32_t i = 0; i < curveData.size(); ++i )
    {
        curveData[i].z = curveData[i].w = tangents[i];
    }
}

//------------------------------------------------------------------------------------------------

static void FixupHermiteTangents( std::vector<ShaderShared::float4>& curveData )
{
    curveData[0].z = 0.0f;
    curveData[0].w *= (curveData[1].x - curveData[0].x);
    uint32_t i;
    for ( i = 1; i < curveData.size() - 1; ++i )
    {
        curveData[i].z *= (curveData[i].x - curveData[i - 1].x);
        curveData[i].w *= (curveData[i + 1].x - curveData[i].x);
    }
    curveData[i].z *= (curveData[i].x - curveData[i - 1].x);
    curveData[i].w = 0.0f;
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadMachSweep( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    std::ifstream iStream( trackedFile.filename );
    if ( iStream.fail() )
    {
        ErrorTrace( trackedFile, L"Failed to open file." );
        return;
    }

    char line[500];
    iStream.getline( line, ARRAY_SIZE( line ) );

    std::vector<std::string> headers;
    char* context;
    for ( const char* token = strtok_s( line, ",", &context ); token; token = strtok_s( nullptr, ",", &context ) )
    {
        headers.emplace_back( token );
        std::for_each(headers.back().begin(), headers.back().end(), [] (char& a) { a = static_cast<char>(tolower(a)); } );
    }

    std::vector<std::vector<double>> data;
    data.emplace_back();
    for (;;)
    {
        std::ifstream::int_type c;
        for (;;)
        {
            c = iStream.peek();
            if ( c == std::ifstream::traits_type::eof() || isdigit( c ) || c == '-' )
                break;
            iStream.get();
        }

        float f;
        iStream >> f;

        if (!iStream.good())
            break;

        if ( data.back().size() < headers.size() )
            data.back().push_back( f );
        else
            data.emplace_back( std::vector<double>( 1, f ) );
    }

    if ( data.size() > 0 )
    {
        ResourceData& resourceData = *static_cast<ResourceData*>(trackedFile.userData);

        for ( uint32_t i = 1; i < data.size(); ++i )
        {
            data.erase( data.begin() + i );
        }

        const char* columnNames[] = { "mach", "a", "cd" };
        double scale = 1.0f;
        if ( resourceData.desc.ptr >= m_shaderHeap.GetCPUHandle( ShaderShared::srvLiftMachCurve ).ptr && resourceData.desc.ptr <= m_shaderHeap.GetCPUHandle( ShaderShared::srvLiftMachCurve, 3 ).ptr )
        {
            columnNames[2] = "cl";
            scale = 1000;
        }

        std::array<uint32_t, ARRAY_SIZE( columnNames )> columnIndex;
        for ( uint32_t i = 0; i < columnIndex.size(); ++i )
        {
            std::vector<std::string>::const_iterator colIter = std::find( headers.cbegin(), headers.cend(), columnNames[i] );
            if ( colIter == headers.cend() )
            {
                ErrorTrace( trackedFile, L"Missing %c%s column.", toupper( *columnNames[i] ), columnNames[i] + 1 );
                return;
            }
            columnIndex[i] = static_cast<uint32_t>(colIter - headers.cbegin());
        }

        std::vector<ShaderShared::float4> curveData;

        curveData.reserve( data.size() );

        for ( const std::vector<double>& src : data )
        {
            float mach = float(src[columnIndex[0]]);
            float y = float(src[columnIndex[2]] * src[columnIndex[1]] * scale);

            curveData.emplace_back( mach, y, 0.0f, 0.0f );
        }

        GenerateMonotonicInterpolants( curveData );
        FixupHermiteTangents( curveData );

        wchar_t name[200];
        swprintf_s( name, L"%s:%SA", trackedFile.filename, columnNames[2] );

        CreateFloat4Buffer( name, resourceData.resource, resourceData.desc, curveData.data(), static_cast<uint32_t>(curveData.size()) );
        resourceData.size = static_cast<uint32_t>(curveData.size());

        // Restart sim
        m_simulationStep = 0;
    }
    else
    {
        ErrorTrace( trackedFile, L"No valid data in file." );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadHermiteCurve( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    try
    {
        nlohmann::json keyListJson = LoadJsonFile( trackedFile.filename );

        std::vector<ShaderShared::float4> curveData;
        curveData.reserve( keyListJson.size() );

        bool generateInterpolants = true;
        for ( const std::vector<float>& key : keyListJson["keys"] )
        {
            if ( key.size() >= 4 )
            {
                curveData.emplace_back( key[0], key[1], key[2], key[3] );
                generateInterpolants = false;
            }
            else if ( key.size() >= 2 )
            {
                curveData.emplace_back( key[0], key[1], 0.0f, 0.0f );
            }
        }

        if ( curveData.size() > 0 )
        {
            if ( curveData.size() > 1 )
            {
                if ( generateInterpolants )
                {
                    GenerateMonotonicInterpolants( curveData );
                }

                FixupHermiteTangents( curveData );
            }

            ResourceData& resourceData = *static_cast<ResourceData*>(trackedFile.userData);

            CreateFloat4Buffer( trackedFile.filename, resourceData.resource, resourceData.desc, curveData.data(), static_cast<uint32_t>(curveData.size()) );
            resourceData.size = static_cast<uint32_t>(curveData.size());
        }
        else
        {
            ErrorTrace( trackedFile, L"No valid data in file." );
        }

        // Restart sim
        m_simulationStep = 0;
    }
    catch ( nlohmann::json::exception& e )
    {
        ErrorTrace( trackedFile, L"%S", e.what() );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::ReloadGraphColours( TrackedFile& trackedFile )
{
    if ( !trackedFile.userData )
        return;

    trackedFile.errorList.clear();

    try
    {
        nlohmann::json coloursJson = LoadJsonFile( trackedFile.filename );

        std::vector<ShaderShared::float4> graphColours;
        graphColours.reserve( coloursJson.size() );

        for ( const std::vector<int>& col : coloursJson["colours"] )
        {
            if ( col.size() >= 3 )
            {
                graphColours.emplace_back( col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f, 1.0f );
            }
        }

        if ( graphColours.size() > 0 )
        {
            ResourceData& resourceData = *static_cast<ResourceData*>(trackedFile.userData);

            CreateFloat4Buffer( trackedFile.filename, resourceData.resource, resourceData.desc, graphColours.data(), static_cast<uint32_t>(graphColours.size()) );
            resourceData.size = static_cast<uint32_t>(graphColours.size());

            m_graphColours.clear();
            m_graphColours.reserve( graphColours.size() );
            std::for_each( graphColours.begin(), graphColours.end(), [this] ( const ShaderShared::float4& col )
            {
                uint32_t ucol = (255 << 24) | (uint32_t( col.z * 255.0f ) << 16) | (uint32_t( col.y * 255.0f ) << 8) | uint32_t( col.x * 255.0f );
                m_graphColours.emplace_back(ucol);
            } );
        }
        else
        {
            ErrorTrace( trackedFile, L"No valid data in file." );
        }
    }
    catch ( nlohmann::json::exception& e )
    {
        ErrorTrace( trackedFile, L"%S", e.what() );
    }

}

//------------------------------------------------------------------------------------------------

void RocketSim::ErrorTrace( TrackedFile& trackedFile, const wchar_t* fmt, ... )
{
    wchar_t message[2000];

    wcscpy_s( message, trackedFile.filename );
    wcscat_s( message, L": " );
    size_t n = wcslen( message );

    va_list args;
    va_start( args, fmt );

    if ( vswprintf_s( message + n, ARRAY_SIZE( message ) - n, fmt, args ) > 0 )
    {
        trackedFile.errorList.emplace_back( message );

        wcscat_s( message, L"\r\n" );
        OutputDebugString( message );
    }

    va_end( args );
}

//------------------------------------------------------------------------------------------------

RocketSim::ErrorResult RocketSim::ErrorHandler( HRESULT hr, const wchar_t* szOperation )
{
    if ( SUCCEEDED( hr ) )
    {
        return ErrorResult::CONTINUE;
    }

    wchar_t szErrorMsg[500];
    swprintf_s( szErrorMsg, L"Could not %s: %d", szOperation, hr );

    MessageBox( m_hwnd, szErrorMsg, szTitle, MB_OK | MB_ICONERROR );
    DestroyWindow( m_hwnd );

    return ErrorResult::ABORT;
}

//------------------------------------------------------------------------------------------------

void RocketSim::UpdateTrackedFiles()
{
    for ( TrackedFile& trackedFile : m_trackedFiles )
    {
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile( trackedFile.filename, &findData );

        if ( hFind != INVALID_HANDLE_VALUE )
        {
            FindClose( hFind );
            if ( CompareFileTime( &trackedFile.lastUpdate, &findData.ftLastWriteTime ) != 0 )
            {
                trackedFile.lastUpdate = findData.ftLastWriteTime;
                (this->*trackedFile.updateCallback)(trackedFile);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::InvalidateTrackedFile( const wchar_t* filename )
{
    for ( TrackedFile& trackedFile : m_trackedFiles )
    {
        if ( !_wcsicmp( filename, trackedFile.filename ) )
        {
            trackedFile.lastUpdate.dwLowDateTime = 0;
            trackedFile.lastUpdate.dwHighDateTime = 0;
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::DrawTrackedFileErrors()
{
    PixMarker marker( m_commandList.Get(), L"DrawTrackedFileErrors" );

    float scale = 0.44f;
    float xpos = -0.98f;
    float ypos = 0.98f - m_font.GetTopAlign() * scale;

    ShaderShared::DrawConstData drawConsts = m_currentDrawConsts;

    for ( TrackedFile& trackedFile : m_trackedFiles )
    {
        for ( const std::wstring& errorLine : trackedFile.errorList )
        {
            if ( m_boxDraw.pso )
            {
                drawConsts.invViewportSize.x = xpos;
                drawConsts.invViewportSize.y = ypos + m_font.GetTopAlign() * scale;
                drawConsts.viewportSize.x = m_font.CalcTextLength( errorLine.c_str() ) * scale;
                drawConsts.viewportSize.y = -m_font.GetLineSpacing() * scale;
                SetDrawConstants( drawConsts );

                m_commandList->SetPipelineState( m_boxDraw.pso.Get() );
                m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
                m_commandList->DrawInstanced( 4, 1, 0, 0 );
            }

            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xff0000df, errorLine.c_str() );
            ypos -= m_font.GetLineSpacing() * scale;

            if ( ypos <= -1.0f )
                return;
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::WaitForGPU()
{
    // Schedule a Signal command in the queue.
    m_commandQueue->Signal( m_fence.Get(), m_fenceValues[m_frameIndex] );

    // Wait until the fence has been processed.
    m_fence->SetEventOnCompletion( m_fenceValues[m_frameIndex], m_fenceEvent );
    WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );

    // Increment the fence value for the current frame.
    ++m_fenceValues[m_frameIndex];
}

//------------------------------------------------------------------------------------------------

void RocketSim::BarrierTransition( ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter )
{
    D3D12_RESOURCE_BARRIER barrier;
    memset( &barrier, 0, sizeof( barrier ) );
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter = stateAfter;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier( 1, &barrier );
}

//------------------------------------------------------------------------------------------------

void RocketSim::BarrierTransition( TrackedResource & resource, D3D12_RESOURCE_STATES newState )
{
    if ( resource.state != newState )
    {
        D3D12_RESOURCE_BARRIER barrier;
        memset( &barrier, 0, sizeof( barrier ) );
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource.resource.Get();
        barrier.Transition.StateBefore = resource.state;
        barrier.Transition.StateAfter = newState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier( 1, &barrier );

        resource.state = newState;
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::BarrierTransition( std::vector<TrackedResource*> resourceList, D3D12_RESOURCE_STATES newState )
{
    if ( resourceList.empty() )
        return;

    D3D12_RESOURCE_BARRIER* barriers = static_cast<D3D12_RESOURCE_BARRIER*>(alloca( sizeof( D3D12_RESOURCE_BARRIER ) * resourceList.size() ));
    memset( barriers, 0, sizeof( D3D12_RESOURCE_BARRIER ) * resourceList.size() );
    uint32_t count = 0;

    for ( TrackedResource* resource : resourceList )
    {
        if ( resource->state != newState )
        {
            barriers[count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[count].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barriers[count].Transition.pResource = resource->resource.Get();
            barriers[count].Transition.StateBefore = resource->state;
            barriers[count].Transition.StateAfter = newState;
            barriers[count].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++count;

            resource->state = newState;
        }
    }

    if (count)
    {
        m_commandList->ResourceBarrier( count, barriers );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::BarrierUAV( std::vector<TrackedResource*> resourceList )
{
    if ( resourceList.empty() )
        return;

    D3D12_RESOURCE_BARRIER* barriers = static_cast<D3D12_RESOURCE_BARRIER*>(alloca( sizeof( D3D12_RESOURCE_BARRIER ) * resourceList.size() ));
    memset( barriers, 0, sizeof( D3D12_RESOURCE_BARRIER ) * resourceList.size() );

    for ( uint_fast32_t i = 0; i < resourceList.size(); ++i )
    {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].UAV.pResource = resourceList[i]->resource.Get();
    }

    m_commandList->ResourceBarrier( static_cast<UINT>(resourceList.size()), barriers );
}

//------------------------------------------------------------------------------------------------

ComPtr<ID3D12Resource> RocketSim::CreateUploadBuffer( const wchar_t* name, const void* data, size_t size )
{
    // Create upload buffer
    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc;
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = size;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> uploadBuffer;

    if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &uploadBuffer ) ), L"CreateUploadBuffer" ) == ErrorResult::ABORT )
        return nullptr;

    uploadBuffer->SetName( name );

    if ( data )
    {
        // Copy the data to the upload buffer.
        UINT8* dataBegin;
        D3D12_RANGE readRange = { 0 };
        if ( ErrorHandler( uploadBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&dataBegin) ), L"Map Buffer" ) == ErrorResult::ABORT )
            return nullptr;
        memcpy( dataBegin, data, size );
        uploadBuffer->Unmap( 0, nullptr );
    }

    return uploadBuffer;
}

//------------------------------------------------------------------------------------------------

ComPtr<ID3D12Resource> RocketSim::CreateReadbackBuffer( const wchar_t* name, size_t size )
{
    // Create readback buffer
    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc;
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = size;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readbackBuffer;

    if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &readbackBuffer ) ), L"CreateReadbackBuffer" ) == ErrorResult::ABORT )
        return nullptr;

    readbackBuffer->SetName( name );

    return readbackBuffer;
}

//------------------------------------------------------------------------------------------------

ComPtr<ID3D12Resource> RocketSim::CreateDefaultBuffer( const wchar_t* name, const void* data, size_t size, D3D12_RESOURCE_STATES targetState, bool allowUAV )
{
    // Create default buffer
    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc;
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = size;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = allowUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> defaultBuffer;

    D3D12_RESOURCE_STATES state = data ? D3D12_RESOURCE_STATE_COPY_DEST : targetState;
    if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, state, nullptr, IID_PPV_ARGS( &defaultBuffer ) ), L"CreateBuffer" ) == ErrorResult::ABORT )
        return nullptr;

    defaultBuffer->SetName( name );

    if ( data )
    {
        // Create upload buffer
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

        ComPtr<ID3D12Resource> uploadBuffer;

        if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &uploadBuffer ) ), L"CreateUploadBuffer" ) == ErrorResult::ABORT )
            return nullptr;

        uploadBuffer->SetName( name );

        // Copy the data to the upload buffer.
        UINT8* dataBegin;
        D3D12_RANGE readRange = { 0 };
        if ( ErrorHandler( uploadBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&dataBegin) ), L"Map Buffer" ) == ErrorResult::ABORT )
            return nullptr;
        memcpy( dataBegin, data, size );
        uploadBuffer->Unmap( 0, nullptr );

        m_pendingUploads.emplace_back( PendingUploadData{ uploadBuffer, defaultBuffer, D3D12_PLACED_SUBRESOURCE_FOOTPRINT{}, targetState } );
    }

    return defaultBuffer;
}

//------------------------------------------------------------------------------------------------

ComPtr<ID3D12Resource> RocketSim::CreateTexture( const wchar_t* name, const void* data, size_t width, size_t height, DXGI_FORMAT format, size_t texelSize, D3D12_RESOURCE_STATES targetState, bool allowUAV )
{
    // Create default buffer
    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.Width = UINT( width );
    textureDesc.Height = UINT( height );
    textureDesc.Flags = allowUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    ComPtr<ID3D12Resource> texture;

    D3D12_RESOURCE_STATES state = data ? D3D12_RESOURCE_STATE_COPY_DEST : targetState;
    if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, state, nullptr, IID_PPV_ARGS( &texture ) ), L"CreateTexture" ) == ErrorResult::ABORT )
        return nullptr;

    texture->SetName( name );

    if ( data )
    {
        // Create upload buffer
        UINT64 bufferSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
        UINT numRows;
        UINT64 rowSize;

        m_device->GetCopyableFootprints( &textureDesc, 0, 1, 0, &layout, &numRows, &rowSize, &bufferSize );

        D3D12_RESOURCE_DESC bufferDesc;
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

        ComPtr<ID3D12Resource> uploadBuffer;

        if ( ErrorHandler( m_device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &uploadBuffer ) ), L"CreateUploadBuffer" ) == ErrorResult::ABORT )
            return nullptr;

        uploadBuffer->SetName( name );

        // Copy the data to the upload buffer.
        BYTE* dataBegin;
        D3D12_RANGE readRange = { 0 };
        if ( ErrorHandler( uploadBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&dataBegin) ), L"Map Buffer" ) == ErrorResult::ABORT )
            return nullptr;

        dataBegin += layout.Offset;

        UINT srcRowPitch = UINT( width * texelSize );
        UINT srcSlicePitch = UINT( srcRowPitch * height );

        UINT dstRowPitch = layout.Footprint.RowPitch;
        UINT dstSlicePitch = dstRowPitch * numRows;

        for ( UINT z = 0; z < layout.Footprint.Depth; ++z )
        {
            BYTE* pDestSlice = dataBegin + dstSlicePitch * z;
            const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(data) + srcSlicePitch * z;

            for ( UINT y = 0; y < numRows; ++y )
            {
                memcpy( pDestSlice + dstRowPitch * y, pSrcSlice + srcRowPitch * y, size_t( rowSize ) );
            }
        }

        uploadBuffer->Unmap( 0, nullptr );

        m_pendingUploads.emplace_back( PendingUploadData{ uploadBuffer, texture, layout, targetState } );
    }

    return texture;
}

//------------------------------------------------------------------------------------------------

ID3D12PipelineState* RocketSim::CreateGraphicsPSO( const wchar_t* name, const D3D12_INPUT_ELEMENT_DESC* inputElements, UINT numInputElements,
                                                   const std::array<ComPtr<ID3DBlob>, ShaderType_Count>& shaderList,
                                                   D3D12_PRIMITIVE_TOPOLOGY_TYPE topology, D3D12_CULL_MODE cullMode,
                                                   const D3D12_RENDER_TARGET_BLEND_DESC& blendDesc )
{
    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElements, numInputElements };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = topology;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    D3D12_SHADER_BYTECODE* byteCode[] = { &psoDesc.VS, &psoDesc.PS, &psoDesc.DS, &psoDesc.HS, &psoDesc.GS };

    for ( uint_fast32_t i = 0; i < ARRAY_SIZE( byteCode ); ++i )
    {
        if ( shaderList[i] )
        {
            byteCode[i]->pShaderBytecode = shaderList[i]->GetBufferPointer();
            byteCode[i]->BytecodeLength = shaderList[i]->GetBufferSize();
        }
    }

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = cullMode;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    for ( UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i )
        psoDesc.BlendState.RenderTarget[i] = blendDesc;

    ID3D12PipelineState* pso = nullptr;
    m_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );

    if ( pso && name )
    {
        pso->SetName( name );
    }

    return pso;
}

//------------------------------------------------------------------------------------------------

ID3D12PipelineState* RocketSim::CreateComputePSO( const wchar_t * name, ID3DBlob* shader )
{
    if ( !shader )
    {
        return nullptr;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS.pShaderBytecode = shader->GetBufferPointer();
    psoDesc.CS.BytecodeLength = shader->GetBufferSize();

    ID3D12PipelineState* pso = nullptr;
    m_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );

    if ( pso )
    {
        pso->SetName( name );
    }

    return pso;
}

//------------------------------------------------------------------------------------------------

static void UpdateDirtyConstants( uint32_t* dst, const uint32_t* src, std::vector<std::pair<uint32_t, uint32_t>>& ranges, uint32_t count )
{
    for ( uint32_t i = 0; i < count; ++i )
    {
        if ( src[i] != dst[i] )
        {
            dst[i] = src[i];

            if ( ranges.empty() || ranges.back().second != i )
            {
                ranges.emplace_back( i, i + 1 );
            }
            else
            {
                ranges.back().second = i + 1;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::SetDrawConstants( const ShaderShared::DrawConstData& drawConsts )
{
    constexpr uint32_t count = sizeof( ShaderShared::DrawConstData ) / sizeof( uint32_t );

    const uint32_t* src = reinterpret_cast<const uint32_t*>(&drawConsts);
    uint32_t* dst = reinterpret_cast<uint32_t*>(&m_currentDrawConsts);

    std::vector<std::pair<uint32_t, uint32_t>> ranges;

    UpdateDirtyConstants(dst, src, ranges, count);

    for ( const std::pair<uint32_t, uint32_t>& r : ranges )
    {
        if ( r.first == r.second )
            m_commandList->SetGraphicsRoot32BitConstant( 0, dst[r.first], r.first );
        else
            m_commandList->SetGraphicsRoot32BitConstants( 0, r.second - r.first, dst + r.first, r.first );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::SetDispatchConstants( const ShaderShared::DispatchConstData& dispatchConsts )
{
    constexpr uint32_t count = sizeof( ShaderShared::DispatchConstData ) / sizeof( uint32_t );

    const uint32_t* src = reinterpret_cast<const uint32_t*>(&dispatchConsts);
    uint32_t* dst = reinterpret_cast<uint32_t*>(&m_currentDispatchConsts);

    std::vector<std::pair<uint32_t, uint32_t>> ranges;

    UpdateDirtyConstants( dst, src, ranges, count );

    for ( const std::pair<uint32_t, uint32_t>& r : ranges )
    {
        if ( r.first == r.second )
            m_commandList->SetComputeRoot32BitConstant( 0, dst[r.first], r.first );
        else
            m_commandList->SetComputeRoot32BitConstants( 0, r.second - r.first, dst + r.first, r.first );
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::RenderFrame()
{
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset( m_commandAllocators[m_frameIndex].Get(), nullptr );

    PixMarker::reset();

    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];

    UpdateTrackedFiles();

    if ( !m_pendingDeletes.empty() )
    {
        for ( std::vector<TrackedResource*>::iterator resource = m_uavResources.begin(); resource != m_uavResources.end(); )
        {
            if ( std::find( m_pendingDeletes.begin(), m_pendingDeletes.end(), (*resource)->resource.Get() ) != m_pendingDeletes.end() )
            {
                resource = m_uavResources.erase( resource );
            }
            else
            {
                ++resource;
            }
        }

        UINT64 completedFence = m_fence->GetCompletedValue();
        m_pendingDeletes.erase( std::remove_if( m_pendingDeletes.begin(), m_pendingDeletes.end(), [completedFence] ( const PendingDeleteData& data )
        {
            return data.fence <= completedFence;
        } ), m_pendingDeletes.end() );
    }

    if ( !m_pendingUploads.empty() )
    {
        PixMarker marker( m_commandList.Get(), L"Upload" );

        for ( PendingUploadData& upload : m_pendingUploads )
        {
            if ( upload.layout.Footprint.Format != DXGI_FORMAT_UNKNOWN )
            {
                D3D12_TEXTURE_COPY_LOCATION dst{ upload.dest.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };
                D3D12_TEXTURE_COPY_LOCATION src{ upload.source.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, upload.layout };
                m_commandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );
            }
            else
            {
                m_commandList->CopyResource( upload.dest.Get(), upload.source.Get() );
            }

            BarrierTransition( upload.dest.Get(), D3D12_RESOURCE_STATE_COPY_DEST, upload.targetState );

            m_pendingDeletes.emplace_back( PendingDeleteData{ upload.source, currentFenceValue } );
        }
        m_pendingUploads.clear();
    }

#ifdef _DEBUG
    if ( m_graphicsAnalysis && m_simulationStep == 0 && m_dispatchList[0].pso )
    {
        m_graphicsAnalysis->BeginCapture();
    }
#endif

    // Update main constants
    m_frameConstData[m_frameIndex]->timeStep = m_simulationStepSize;
    m_frameConstData[m_frameIndex]->simDataStep = m_simulationThreadCount;
    m_frameConstData[m_frameIndex]->selectedData = m_selectedData;
    m_frameConstData[m_frameIndex]->selectionFlash = int( GetTimestampMilliSec() / 500.0f ) & 1;

    // Set necessary state.
    ID3D12DescriptorHeap* ppHeaps[] = { m_shaderHeap.GetHeap() };
    m_commandList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

    {
        PixMarker marker( m_commandList.Get(), L"Simulation" );

        m_commandList->SetComputeRootSignature( m_rootSignature.Get() );
        m_commandList->SetComputeRootConstantBufferView( 1, m_frameConstantBuffer->GetGPUVirtualAddress() + m_frameIndex * RoundUpPow2( sizeof( ShaderShared::FrameConstData ), 256llu ) );
        m_commandList->SetComputeRootDescriptorTable( 2, m_shaderHeap.GetHeap()->GetGPUDescriptorHandleForHeapStart() );

        BarrierTransition( m_uavResources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

        if ( m_queryHeap && m_frameIndex == 0 )
            m_commandList->EndQuery( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0 );

        // Mark current consts invalid
        memset( &m_currentDispatchConsts, 0xde, sizeof( m_currentDispatchConsts ) );

        ShaderShared::DispatchConstData dispatchConsts;

        // Run N sim steps
        for ( uint32_t i = 0; i < m_simStepsPerFrame && m_simulationStep < (m_telemetryMaxSamples * m_telemetryStepSize); ++i )
        {
            dispatchConsts.srcDataOffset = ((m_simulationStep - 1) / m_telemetryStepSize) * m_simulationThreadCount;
            dispatchConsts.dstDataOffset = (m_simulationStep / m_telemetryStepSize) * m_simulationThreadCount;
            if ( m_simulationStep == 0 )
                dispatchConsts.srcDataOffset = ~0u;
            SetDispatchConstants( dispatchConsts );

            bool simRunning = true;
            for ( const DispatchData& dispatchData : m_dispatchList )
            {
                if ( dispatchData.perStep )
                {
                    if ( dispatchData.pso && dispatchData.threadGroupCount[0] > 0 )
                    {
                        m_commandList->SetPipelineState( dispatchData.pso.Get() );

                        m_commandList->Dispatch( dispatchData.threadGroupCount[0], dispatchData.threadGroupCount[1], dispatchData.threadGroupCount[2] );

                        BarrierUAV( m_uavResources );
                    }
                    else
                    {
                        simRunning = false;
                    }
                }
            }

            if ( simRunning )
            {
                ++m_simulationStep;
            }
            else
            {
                break;
            }
        }
    }

    {
        PixMarker marker( m_commandList.Get(), L"Sim Per Frame" );

        BarrierTransition( m_uavResources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

        // Do per frame dispatches
        for ( const DispatchData& dispatchData : m_dispatchList )
        {
            if ( !dispatchData.perStep && dispatchData.pso && dispatchData.threadGroupCount[0] > 0 )
            {
                m_commandList->SetPipelineState( dispatchData.pso.Get() );

                m_commandList->Dispatch( dispatchData.threadGroupCount[0], dispatchData.threadGroupCount[1], dispatchData.threadGroupCount[2] );

                BarrierUAV( m_uavResources );
            }
        }

        if ( m_simulationStep > 0 )
        {
            BarrierTransition( m_flightData, D3D12_RESOURCE_STATE_COPY_SOURCE );

            m_commandList->CopyResource( m_flightDataRB[m_frameIndex].Get(), m_flightData.resource.Get() );
        }

        BarrierTransition( m_uavResources, D3D12_RESOURCE_STATE_SHADER_RESOURCE );

        if ( m_queryHeap && m_frameIndex == 0 )
        {
            m_commandList->EndQuery( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1 );

            m_commandList->ResolveQueryData( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_queryRB.Get(), 0 );
        }
    }

    {
        PixMarker marker( m_commandList.Get(), L"GFX Setup" );

        m_commandList->SetGraphicsRootSignature( m_rootSignature.Get() );
        m_commandList->SetGraphicsRootConstantBufferView( 1, m_frameConstantBuffer->GetGPUVirtualAddress() + m_frameIndex * RoundUpPow2( sizeof( ShaderShared::FrameConstData ), 256llu ) );
        m_commandList->SetGraphicsRootDescriptorTable( 2, m_shaderHeap.GetHeap()->GetGPUDescriptorHandleForHeapStart() );

		m_graphViewport = m_viewport;
		m_graphViewport.Width = m_viewport.Width * 8 / 10;
		m_graphViewport.TopLeftX = m_viewport.Width - m_graphViewport.Width;

		m_heatmapViewport = m_viewport;
		m_heatmapViewport.Width = m_graphViewport.TopLeftX * 0.5f - 1;
		m_heatmapViewport.Height = m_graphViewport.TopLeftX;

        // Mark current consts invalid
        memset( &m_currentDrawConsts, 0xde, sizeof(m_currentDrawConsts) );

        // Set default draw constants
        ShaderShared::DrawConstData drawConsts;
        drawConsts.graphDataOffset = 0;
        drawConsts.graphDiscontinuity = static_cast<float>(m_simulationStep / m_telemetryStepSize);
        drawConsts.graphViewportScale = 5.0f / m_viewport.Width;    // NumPoints = 2 / graphViewportScale
        drawConsts.graphSampleXScale = m_telemetryMaxSamples * drawConsts.graphViewportScale / 2.0f;

        drawConsts.viewportSize.x = m_graphViewport.Width;
        drawConsts.viewportSize.y = m_graphViewport.Height;
        drawConsts.invViewportSize.x = 1.0f / drawConsts.viewportSize.x;
        drawConsts.invViewportSize.y = 1.0f / drawConsts.viewportSize.y;

        SetDrawConstants( drawConsts );

        SetViewport( m_viewport );
        m_commandList->RSSetScissorRects( 1, &m_scissorRect );

        // Indicate that the back buffer will be used as a render target.
        BarrierTransition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap.GetCPUHandle( m_frameIndex );
        m_commandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

        // Record commands.
        const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        m_commandList->ClearRenderTargetView( m_rtvHeap.GetCPUHandle( m_frameIndex ), clearColor, 0, nullptr );

        m_font.UpdateLoading();
    }

    {
        PixMarker marker( m_commandList.Get(), L"Graph Draw" );

        SetViewport( m_graphViewport );

        ShaderShared::DrawConstData drawConsts = m_currentDrawConsts;

        if ( m_graphBackground.pso )
        {
            drawConsts.graphSampleXScale = float(m_telemetryMaxSamples - 1);
            SetDrawConstants( drawConsts );

            m_commandList->SetPipelineState( m_graphBackground.pso.Get() );
            m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
            m_commandList->DrawInstanced( 4, 1, 0, 0 );
        }


        float scale = 0.35f;
        float xpos = -0.99f;
        float ypos = 0.98f - m_font.GetTopAlign() * scale;

        for ( GraphList graphIdx : m_graphLayout )
        {
            if (graphIdx != GraphList_Count)
            {
                GraphData& graphData = m_graphList[graphIdx];

                if ( graphData.pso )
                {
                    drawConsts.graphDiscontinuity = static_cast<float>(m_simulationStep / m_telemetryStepSize);
                    drawConsts.graphSampleXScale = m_telemetryMaxSamples * m_currentDrawConsts.graphViewportScale / 2.0f;
                    SetDrawConstants( drawConsts );

                    m_commandList->SetPipelineState( graphData.pso.Get() );

                    m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINESTRIP );

                    uint32_t vertexCount = static_cast<uint32_t>(ceilf( 2.0f / drawConsts.graphViewportScale ));

                    m_commandList->DrawInstanced( vertexCount, 1, 0, 0 );

                    wchar_t legend[100];
                    wcscpy_s( legend, wcschr( graphData.name, '_' ) + 1);
                    *wcsstr( legend, L"_vtx" ) = 0;

                    m_font.DrawText( xpos, ypos, scale, 0.0f, m_graphColours[graphIdx], legend );
                    ypos -= m_font.GetLineSpacing() * scale;
                }
            }
        }
    }

	{
		PixMarker marker(m_commandList.Get(), L"Heatmap Draw");

        D3D12_VIEWPORT hmViewport = m_heatmapViewport;

		ShaderShared::DrawConstData drawConsts = m_currentDrawConsts;

		for ( HeatmapList heatmapIdx : m_heatmapLayout )
		{
			if (heatmapIdx != HeatmapList_Count)
			{
				HeatmapData& heatmapData = m_heatmapList[heatmapIdx];

				if ( heatmapData.pso)
				{
                    SetViewport( hmViewport );

                    if ( hmViewport.TopLeftX == m_viewport.TopLeftX )
                    {
                        hmViewport.TopLeftX += hmViewport.Width + c_xHeatmapSpacing;
                    }
                    else
                    {
                        hmViewport.TopLeftX = m_viewport.TopLeftX;
                        hmViewport.TopLeftY += hmViewport.Height + c_yHeatmapSpacing;
                    }
                    
					m_commandList->SetPipelineState( heatmapData.pso.Get());

					m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					m_commandList->DrawInstanced(3, 1, 0, 0);
				}

                if ( m_selectionMarker.pso )
                {
                    m_commandList->SetPipelineState( m_selectionMarker.pso.Get() );

                    m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINELIST );

                    m_commandList->DrawInstanced( 16, 1, 0, 0 );
                }
			}
		}

        SetViewport( m_viewport );

        if ( m_mouseCapture && m_boxDraw.pso )
        {
            drawConsts.invViewportSize.x = 2.0f * m_mouseDragStart.x / m_viewport.Width - 1.0f;
            drawConsts.invViewportSize.y = 1.0f - 2.0f * m_mouseDragStart.y / m_viewport.Height;
            drawConsts.viewportSize.x = 2.0f * (m_mouseDragEnd.x - m_mouseDragStart.x) / m_viewport.Width;
            drawConsts.viewportSize.y = 2.0f * (m_mouseDragStart.y - m_mouseDragEnd.y) / m_viewport.Height;
            SetDrawConstants( drawConsts );

            m_commandList->SetPipelineState( m_boxDraw.pso.Get() );
            m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
            m_commandList->DrawInstanced( 4, 1, 0, 0 );
        }
    }

    {
        PixMarker marker( m_commandList.Get(), L"Heatmap Legend" );

        float scale = 0.3f;
        float xpos = -0.99f;
        float ypos = 1.0f - 2.0f * ((m_heatmapViewport.Height + 1) / m_viewport.Height) - m_font.GetTopAlign() * scale;

        for ( HeatmapList heatmapIdx : m_heatmapLayout )
        {
            if ( heatmapIdx != HeatmapList_Count )
            {
                wchar_t legend[100];
                wcscpy_s( legend, wcschr( m_heatmapList[heatmapIdx].name, '_' ) + 1 );
                *wcsstr( legend, L"_pxl" ) = 0;

                m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, legend );

                if ( xpos <= -0.98f )
                {
                    xpos += 2.0f * ((m_heatmapViewport.Width + c_xHeatmapSpacing) / m_viewport.Width);
                }
                else
                {
                    xpos = -0.99f;
                    ypos -= 2.0f * (m_heatmapViewport.Height + c_yHeatmapSpacing) / m_viewport.Height;
                }
            }
        }

        scale = 0.4f;
        ypos += 2.0f * m_heatmapViewport.Height / m_viewport.Height;

        const ShaderShared::AscentParams& ascentParams = m_ascentParams[m_selectedData / m_ascentParams[0].size()][m_selectedData % m_ascentParams[0].size()];

        wchar_t statusText[100];
        swprintf_s( statusText, L"Pitch Over Angle: %.2f\xb0", acos( ascentParams.cosPitchOverAngle) * c_RadToDegree );
        m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

        float spacing = m_font.GetLineSpacing() * scale * 1.1f;
        ypos -= spacing;
        swprintf_s( statusText, L"Pitch Over Speed: %.1f m/s", ascentParams.pitchOverSpeed );
        m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

        if ( m_simulationStep > 0 )
        {
            std::vector<ShaderShared::FlightData> flightData;
            flightData.resize( m_simulationThreadCount + 2, ShaderShared::FlightData{} );

            UINT8* dataBegin;
            D3D12_RANGE readRange = { 0 };
            if ( ErrorHandler( m_flightDataRB[1 - m_frameIndex]->Map( 0, &readRange, reinterpret_cast<void**>(&dataBegin) ), L"Map Buffer" ) == ErrorResult::CONTINUE )
            {
                memcpy( flightData.data(), dataBegin, flightData.size() * sizeof( ShaderShared::FlightData ) );
                m_flightDataRB[1 - m_frameIndex]->Unmap( 0, nullptr );
            }

            if ( m_autoSelectData && m_simulationStep == (m_telemetryMaxSamples * m_telemetryStepSize))
            {
                float bestDelta = FLT_MAX;
                uint32_t selected = ~0u;

                for ( uint32_t i = 0; i < m_simulationThreadCount; ++i )
                {
                    if ( flightData[i].flightPhase == ShaderShared::c_PhaseMECO && flightData[i].maxQ < 60000.0f )
                    {
                        float massDelta = flightData[m_simulationThreadCount + 1].minMass - flightData[i].minMass;
                        massDelta = std::max( massDelta, 0.0f );

                        float targetPe = float(m_missionParams.finalState.r - earthRadius);
                        float targetAp = float(-earthMu / (2 * m_missionParams.finalOrbitalEnergy) - earthRadius) * 2 - targetPe;

                        float Ap = (1 + flightData[i].e) * flightData[i].a - float(earthRadius);
                        float Pe = (1 - flightData[i].e) * flightData[i].a - float(earthRadius);

                        float orbDelta = sqrtf(sqr( Ap - targetAp ) + sqr( Pe - targetPe ));

                        float delta = massDelta + sqr(orbDelta * 0.001f) * 10.0f;

                        if ( delta < bestDelta )
                        {
                            bestDelta = delta;
                            selected = i;
                        }
                    }
                }

                if ( selected < m_simulationThreadCount )
                    m_selectedData = selected;
            }

            ypos -= spacing;
            swprintf_s( statusText, L"Max Altitude: %.2f km", flightData[m_selectedData].maxAltitude / 1000.0f );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Max Surface Speed: %.1f m/s", flightData[m_selectedData].maxSurfSpeed );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Max Q: %.2f kPa", flightData[m_selectedData].maxQ / 1000.0f );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Max g: %.2f", flightData[m_selectedData].maxAccel / earthg0 );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Min Mass: %.3f tonnes", flightData[m_selectedData].minMass / 1000.0f );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            // Calc delta v for current stage
            uint32_t stage = flightData[m_selectedData].stage;
            float deltaV = (float(earthg0) * m_missionParams.stage[stage].IspVac) * logf( flightData[m_selectedData].minMass / m_missionParams.stage[stage].dryMass );

            // Add on delta v for remaining stages
            for ( uint32_t s = stage + 1; s < m_missionParams.stageCount; ++s )
            {
                deltaV += (float(earthg0) * m_missionParams.stage[s].IspVac) * logf( m_missionParams.stage[s].wetMass / m_missionParams.stage[s].dryMass );
            }

            // Calc original delta V
            float padDeltaV = 0.0f;
            for ( uint32_t s = 0; s < m_missionParams.stageCount; ++s )
            {
                padDeltaV += (float( earthg0 ) * m_missionParams.stage[s].IspVac) * logf( m_missionParams.stage[s].wetMass / m_missionParams.stage[s].dryMass );
            }

            ypos -= spacing;
            swprintf_s( statusText, L"Delta V: %.1f / %.1f m/s", deltaV, padDeltaV );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            const wchar_t* flightPhases[] = { L"Liftoff", L"Pitch Over", L"Aero Flight", L"Guidance Ready", L"Guidance Active", L"MECO" };

            ypos -= spacing;
            swprintf_s( statusText, L"Final Flight Phase: %s", flightPhases[flightData[m_selectedData].flightPhase] );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Apoapsis: %.3f km", ((1.0f + flightData[m_selectedData].e) * flightData[m_selectedData].a - 6371000.0f) / 1000.0f);
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            ypos -= spacing;
            swprintf_s( statusText, L"Periapsis: %.3f km", ((1.0f - flightData[m_selectedData].e) * flightData[m_selectedData].a - 6371000.0f) / 1000.0f );
            m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

            for ( uint32_t s = 0; s < m_missionParams.stageCount; ++s )
            {
                ypos -= spacing;
                swprintf_s( statusText, L"Stage %d Burn: %.1f s", s, flightData[m_selectedData].stageBurnTime[s] );
                m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );
            }
        }
    }

    {
        PixMarker marker( m_commandList.Get(), L"Status Text" );

        float scale = 0.44f;

        wchar_t statusText[100];
        swprintf_s( statusText, L"Simulation Time: %.2fs (steps=%u, sim=%.2f ms)", m_simulationStep * m_simulationStepSize, m_simStepsPerFrame, m_simGpuTime.getAverage() );
        float xpos = 0.98f - m_font.CalcTextLength( statusText ) * scale;
        float ypos = 0.98f - m_font.GetTopAlign() * scale;
        m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );

        float time = GetTimestampMilliSec();
        if ( m_lastFrame > 0.0f )
        {
            m_frameTime = m_frameTime * 0.9f + (time - m_lastFrame) * 0.1f;
        }
        m_lastFrame = time;

        ypos -= m_font.GetLineSpacing() * scale;
        swprintf_s( statusText, L"Frame Rate: %.1f", 1000.0f / m_frameTime );
        m_font.DrawText( xpos, ypos, scale, 0.0f, 0xffcfcfcf, statusText );
    }

    DrawTrackedFileErrors();

    // Indicate that the back buffer will now be used to present.
    BarrierTransition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );

    m_commandList->Close();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    // Present the frame.
    m_swapChain->Present( 1, 0 );

#ifdef _DEBUG
    if ( m_graphicsAnalysis && m_simulationStep == (m_telemetryMaxSamples * m_telemetryStepSize) && m_dispatchList[0].pso )
    {
        m_graphicsAnalysis->EndCapture();
    }
#endif

    // Schedule a Signal command in the queue.
    m_commandQueue->Signal( m_fence.Get(), currentFenceValue );

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if ( m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex] )
    {
        m_fence->SetEventOnCompletion( m_fenceValues[m_frameIndex], m_fenceEvent );
        WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;

    // Update frame step speed
    if ( m_simulationStep > 0 && m_simulationStep < (m_telemetryMaxSamples * m_telemetryStepSize) && m_queryHeap && m_frameIndex == 1 )
    {
        int64_t simulationTime = -1;

        void* dataBegin;
        if ( ErrorHandler( m_queryRB->Map( 0, nullptr, &dataBegin ), L"Map Buffer" ) == ErrorResult::CONTINUE )
        {
            const int64_t* timestamp = static_cast<const int64_t*>(dataBegin);
            simulationTime = timestamp[1] - timestamp[0];
            D3D12_RANGE writeRange = { 0 };
            m_queryRB->Unmap( 0, &writeRange );
        }

        uint64_t gpuFreq = 0;
        if ( simulationTime > 0 && m_commandQueue->GetTimestampFrequency( &gpuFreq ) == S_OK )
        {
            double simTimeMS = 1000.0 * simulationTime / gpuFreq;

            if ( m_simGpuTime.getAverage() == 0.0f )
                m_simGpuTime.reset( static_cast<float>(simTimeMS) );
            else
                m_simGpuTime.addData( static_cast<float>(simTimeMS) );

            if ( m_tweakTimer > 0 )
                --m_tweakTimer;

            // Only update when the timings are fairly stable.
            if ( m_simGpuTime.getAverage() > m_simGpuTime.getStdDeviation() )
            {
                float maxTime = m_simGpuTime.getAverage() + m_simGpuTime.getStdDeviation();

				uint32_t newSteps = 0;
                if ( maxTime < 2.0f )
                {
                    // Less than 5 ms, increase rapidly
					newSteps = m_simStepsPerFrame * 5 / 4;
					newSteps = std::max(newSteps, m_simStepsPerFrame + 1);
                }
                else if ( !m_tweakTimer && maxTime < 8.0f )
                {
                    // Less than 10 ms, increase slowly
					newSteps = m_simStepsPerFrame * 16 / 15;
					newSteps = std::max(newSteps, m_simStepsPerFrame + 1);
				}
                else if ( maxTime > 16.0f )
                {
                    // Over 20 ms, reduce rapidly
					newSteps = m_simStepsPerFrame * 3 / 4;
					newSteps = std::min(newSteps, m_simStepsPerFrame - 1);
				}
                else if ( !m_tweakTimer && maxTime > 12.0f )
                {
                    // Over 15 ms, reduce slowly
					newSteps = m_simStepsPerFrame * 15 / 16;
					newSteps = std::min(newSteps, m_simStepsPerFrame - 1);
				}

				if (newSteps)
				{
					m_simStepsPerFrame = newSteps;
					m_tweakTimer = 10;
				}
            }

            if ( m_simStepsPerFrame > 1000 )
            {
                m_simStepsPerFrame = ((m_simStepsPerFrame + 50) / 100) * 100;
            }
            else if ( m_simStepsPerFrame > 500 )
            {
                m_simStepsPerFrame = ((m_simStepsPerFrame + 25) / 50) * 50;
            }
            else if ( m_simStepsPerFrame > 100 )
            {
                m_simStepsPerFrame = ((m_simStepsPerFrame + 5) / 10) * 10;
            }
            else if ( m_simStepsPerFrame > 50 )
            {
                m_simStepsPerFrame = ((m_simStepsPerFrame + 2) / 5) * 5;
            }

            m_simStepsPerFrame = std::min( m_simStepsPerFrame, m_telemetryMaxSamples / 10 );
            m_simStepsPerFrame = std::max( m_simStepsPerFrame, 10u );
        }
    }
}

//------------------------------------------------------------------------------------------------

void RocketSim::Shutdown()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGPU();

    CloseHandle( m_fenceEvent );
}

//------------------------------------------------------------------------------------------------

float RocketSim::xPositionToDataIndex(int x)
{
    x -= int(m_heatmapViewport.TopLeftX + 0.5);

    if ( x < 0 || x >= int( m_heatmapViewport.Width + c_xHeatmapSpacing ) * 2 )
        return -1.0f;

    x %= int( m_heatmapViewport.Width + c_xHeatmapSpacing );

    return x * (m_simulationThreadWidth - 1) / (m_heatmapViewport.Width - 1);
}

float RocketSim::yPositionToDataIndex(int y)
{
    y -= int( m_heatmapViewport.TopLeftY + 0.5 );

    if ( y < 0 || y >= int( m_heatmapViewport.Height + c_yHeatmapSpacing ) * 2 )
        return -1.0f;

    y %= int( m_heatmapViewport.Height + c_yHeatmapSpacing );

    return y * (m_simulationThreadHeight - 1) / (m_heatmapViewport.Height - 1);
}

//------------------------------------------------------------------------------------------------

void RocketSim::MouseClicked( int x, int y, bool shiftPressed, bool )
{
    if ( shiftPressed || m_mouseCapture )
    {
        if ( !m_mouseCapture && DragDetect( m_hwnd, POINT{ x,y } ) )
        {
            m_mouseDragStart.x = x;
            m_mouseDragStart.y = y;
            SetCapture( m_hwnd );
            m_mouseCapture = true;
        }
        
        if ( m_mouseCapture )
        {
            m_mouseDragEnd.x = x;
            m_mouseDragEnd.y = y;
        }

        return;
    }

    x = int( xPositionToDataIndex( x ) + 0.5f );
    y = int( yPositionToDataIndex( y ) + 0.5f );

    if ( x >= 0 && x < int( m_simulationThreadWidth ) && y >= 0 && y < int( m_simulationThreadHeight ) )
    {
        m_selectedData = x + (m_simulationThreadHeight - y - 1) * m_simulationThreadWidth;
        m_autoSelectData = false;
    }
}

void RocketSim::MouseReleased( int x, int y, bool captureLost )
{
    if ( m_mouseCapture && !captureLost )
    {
        ReleaseCapture();

        m_mouseDragEnd.x = x;
        m_mouseDragEnd.y = y;

        float xStart = xPositionToDataIndex( m_mouseDragStart.x );
        float yStart = yPositionToDataIndex( m_mouseDragStart.y );

        if ( xStart > -1.0f && xStart <= m_simulationThreadWidth && yStart > -1.0f && yStart <= m_simulationThreadHeight )
        {
            int xMax = m_simulationThreadWidth - 1;
            int yMax = m_simulationThreadHeight - 1;

            xStart = std::min( std::max( xStart, 0.0f ), float(xMax) );
            yStart = std::min( std::max( yStart, 0.0f ), float(yMax) );
            
            float f = xStart - floorf( xStart );
            float minAngle = asin( m_ascentParams[0][int(xStart)].sinPitchOverAngle * (1 - f) + m_ascentParams[0][std::min(int(xStart) + 1, xMax)].sinPitchOverAngle * f );
            f = yStart - floorf( yStart );
            float minSpeed = m_ascentParams[m_simulationThreadHeight - 1 - int(yStart)][0].pitchOverSpeed * (1 - f) + m_ascentParams[std::max( int( m_simulationThreadHeight ) - 1 - (int(yStart) + 1), 0)][0].pitchOverSpeed * f;

            float xEnd = xPositionToDataIndex( m_mouseDragEnd.x );
            xEnd = std::min( std::max( xEnd, 0.0f ), m_simulationThreadWidth - 1.0f );
            float yEnd = yPositionToDataIndex( m_mouseDragEnd.y );
            yEnd = std::min( std::max( yEnd, 0.0f ), m_simulationThreadHeight - 1.0f );

            f = xEnd - floorf( xEnd );
            float maxAngle = asin( m_ascentParams[0][int(xEnd)].sinPitchOverAngle * (1 - f) + m_ascentParams[0][std::min(int(xEnd) + 1, xMax)].sinPitchOverAngle * f );
            f = yEnd - floorf( yEnd );
            float maxSpeed = m_ascentParams[m_simulationThreadHeight - 1 - int(yEnd)][0].pitchOverSpeed * (1 - f) + m_ascentParams[std::max( int(m_simulationThreadHeight) - 1 - (int(yEnd) + 1), 0)][0].pitchOverSpeed * f;

            if ( maxAngle < minAngle )
                std::swap( minAngle, maxAngle );
            if ( maxSpeed < minSpeed )
                std::swap( minSpeed, maxSpeed );

            float angleStep = (maxAngle - minAngle) / float( m_ascentParams[0].size() - 1 );
            float speedStep = (maxSpeed - minSpeed) / float( m_ascentParams.size() - 1 );

            for ( uint32_t angle = 0; angle < m_ascentParams[0].size(); ++angle )
            {
                float a = minAngle + angle * angleStep;
                m_ascentParams[0][angle].pitchOverSpeed = minSpeed;
                m_ascentParams[0][angle].sinPitchOverAngle = sin( a );
                m_ascentParams[0][angle].cosPitchOverAngle = cos( a );
                m_ascentParams[0][angle].sinAimAngle = sin( a + c_DegreeToRad );
                m_ascentParams[0][angle].cosAimAngle = cos( a + c_DegreeToRad );
            }

            for ( uint32_t speed = 1; speed < m_ascentParams.size(); ++speed )
            {
                float pitchOverSpeed = minSpeed + speed * speedStep;
                for ( uint32_t angle = 0; angle < m_ascentParams[0].size(); ++angle )
                {
                    m_ascentParams[speed][angle].pitchOverSpeed = pitchOverSpeed;
                    m_ascentParams[speed][angle].sinPitchOverAngle = m_ascentParams[speed - 1][angle].sinPitchOverAngle;
                    m_ascentParams[speed][angle].cosPitchOverAngle = m_ascentParams[speed - 1][angle].cosPitchOverAngle;
                    m_ascentParams[speed][angle].sinAimAngle = m_ascentParams[speed - 1][angle].sinAimAngle;
                    m_ascentParams[speed][angle].cosAimAngle = m_ascentParams[speed - 1][angle].cosAimAngle;
                }
            }

            ResourceData* resourceData = &m_ascentParamsBuffer;
            CreateStructuredBuffer( L"Zoomed ascent params", resourceData->resource, resourceData->desc, m_ascentParams.data(),
                                    sizeof( ShaderShared::AscentParams ), static_cast<uint32_t>(m_ascentParams.size() * m_ascentParams[0].size()) );

            // Restart sim
            m_simulationStep = 0;
            m_autoSelectData = true;
        }
    }
    m_mouseCapture = false;
}

//------------------------------------------------------------------------------------------------

void RocketSim::RMouseClicked( int x, int y, bool, bool )
{
    x = int( xPositionToDataIndex( x ) + 0.5f );
    y = int( yPositionToDataIndex( y ) + 0.5f );

    if ( x >= 0 && x < int( m_simulationThreadWidth ) && y >= 0 && y < int( m_simulationThreadHeight ) )
    {
        for ( TrackedFile& tf : m_trackedFiles )
        {
            if ( tf.updateCallback == &RocketSim::ReloadAscentParams )
            {
                (this->*tf.updateCallback)(tf);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass( HINSTANCE hInstance );
BOOL                InitInstance( HINSTANCE, int );
LRESULT CALLBACK    WndProc( HWND, UINT, WPARAM, LPARAM );
INT_PTR CALLBACK    About( HWND, UINT, WPARAM, LPARAM );

//------------------------------------------------------------------------------------------------

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    // Initialize global strings
    LoadStringW( hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING );
    LoadStringW( hInstance, IDC_ROCKETSIM, szWindowClass, MAX_LOADSTRING );
    MyRegisterClass( hInstance );

    // Perform application initialization:
    if ( !InitInstance( hInstance, nCmdShow ) )
    {
        return FALSE;
    }

    MSG msg = {};
    while ( msg.message != WM_QUIT )
    {
        // Process any messages in the queue.
        if ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
    }

    s_RocketSim.Shutdown();

    return (int)msg.wParam;
}

//------------------------------------------------------------------------------------------------

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass( HINSTANCE hInstance )
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof( WNDCLASSEX );

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon( hInstance, MAKEINTRESOURCE( IDI_ROCKETSIM ) );
    wcex.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW( IDC_ROCKETSIM );
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon( wcex.hInstance, MAKEINTRESOURCE( IDI_SMALL ) );

    return RegisterClassExW( &wcex );
}

//------------------------------------------------------------------------------------------------

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance( HINSTANCE hInstance, int nCmdShow )
{
    RECT workArea;
    SystemParametersInfo( SPI_GETWORKAREA, 0, &workArea, 0 );

    RECT windowRect;
    UINT width = (workArea.right - workArea.left) & ~3;
    UINT height = 0;

    do
    {
        width -= 4;

        while ( width > 0 )
        {
            height = width * 9 / 16;
            if ( (height & 3) == 0 )
            {
                break;
            }
            width -= 4;
        }

        windowRect.left = 0;
        windowRect.top = 0;
        windowRect.right = width;
        windowRect.bottom = height;
        AdjustWindowRect( &windowRect, WS_OVERLAPPEDWINDOW, FALSE );

    } while ( (windowRect.right - windowRect.left) > (workArea.right - workArea.left) || (windowRect.bottom - windowRect.top) > (workArea.bottom - workArea.top) );

    width = windowRect.right - windowRect.left;
    height = windowRect.bottom - windowRect.top;

    UINT left = ((workArea.right - workArea.left) - width) / 2;
    UINT top = ((workArea.bottom - workArea.top) - height) / 2;

    HWND hWnd = CreateWindowW( szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, left, top, width, height, nullptr, nullptr, hInstance, nullptr );

    if ( !hWnd )
    {
        return FALSE;
    }

    s_RocketSim.InitRenderer( hWnd );
    s_RocketSim.LoadBaseResources();

    ShowWindow( hWnd, nCmdShow );

    return TRUE;
}

//------------------------------------------------------------------------------------------------

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
        case WM_COMMAND:
        {
            int wmId = LOWORD( wParam );
            // Parse the menu selections:
            switch ( wmId )
            {
                case IDM_ABOUT:
                    DialogBox( GetModuleHandle( nullptr ), MAKEINTRESOURCE( IDD_ABOUTBOX ), hWnd, About );
                    break;
                case IDM_EXIT:
                    DestroyWindow( hWnd );
                    break;
                default:
                    return DefWindowProc( hWnd, message, wParam, lParam );
            }
        }
        break;

        case WM_PAINT:
            s_RocketSim.RenderFrame();
            return 0;

        case WM_LBUTTONDOWN:
            s_RocketSim.MouseClicked( GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ), (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0 );
            return 0;

        case WM_LBUTTONUP:
            s_RocketSim.MouseReleased( GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ), false );
            return 0;

        case WM_RBUTTONUP:
            s_RocketSim.RMouseClicked( GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ), (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0 );
            return 0;

        case WM_MOUSEMOVE:
            if ( wParam & MK_LBUTTON )
                s_RocketSim.MouseClicked( GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ), (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0 );
            return 0;

        case WM_CAPTURECHANGED:
            s_RocketSim.MouseReleased( GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ), true );
            break;

        case WM_DESTROY:
            PostQuitMessage( 0 );
            break;

        default:
            return DefWindowProc( hWnd, message, wParam, lParam );
    }
    return 0;
}

//------------------------------------------------------------------------------------------------

// Message handler for about box.
INT_PTR CALLBACK About( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam )
{
    UNREFERENCED_PARAMETER( lParam );
    switch ( message )
    {
        case WM_INITDIALOG:
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if ( LOWORD( wParam ) == IDOK || LOWORD( wParam ) == IDCANCEL )
            {
                EndDialog( hDlg, LOWORD( wParam ) );
                return (INT_PTR)TRUE;
            }
            break;
    }
    return (INT_PTR)FALSE;
}
