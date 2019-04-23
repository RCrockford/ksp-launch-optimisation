// Helper funcs for graph drawing

struct PSInput
{
    float4 position : SV_POSITION;
    float4 colour : COLOUR;
};

StructuredBuffer<TelemetryData>   g_TelemetryData  : register(t[srvTelemetryData]);
StructuredBuffer<FlightData>      g_FlightData     : register(t[srvFlightData]);

ConstantBuffer<MissionParams>       c_missionParams : register(b[cbvMissionParams]);

Buffer<float4>  g_graphColours  : register(t[srvGraphColours]);

static const uint GraphColour_Height = 0;
static const uint GraphColour_Velocity = 1;
static const uint GraphColour_Q = 2;
static const uint GraphColour_Mass = 3;
static const uint GraphColour_GuidancePitch = 4;
static const uint GraphColour_Pitch = 5;
static const uint GraphColour_Apoapsis = 6;
static const uint GraphColour_Eccentricty = 7;
static const uint GraphColour_TWR = 8;
static const uint GraphColour_AoA = 9;
static const uint GraphColour_Position = 10;


float GetGraphSample(uint idx)
{
    return idx * c_drawConsts.graphSampleXScale;
}

float GetTelemetrySamples(float x, uint instId, out int s1, out int s2)
{
    uint dataLen, dataStride;
    g_TelemetryData.GetDimensions(dataLen, dataStride);
    
    float f = modf(x, s1);
    
    s1 = s1 * c_frameConsts.simDataStep + c_drawConsts.graphDataOffset + c_frameConsts.selectedData;
    s2 = s1 + c_frameConsts.simDataStep;
    
    if (s1 < 0)
        f = 1.0;
    else if (s2 >= int(dataLen))
        f = 0.0;
        
    s1 = clamp(s1, 0, dataLen - 1);
    s2 = clamp(s2, 0, dataLen - 1);
    
    return f;
}

bool CheckDiscontinuity(float x)
{
    // Check for crossing discontinuity.
    if (x < c_drawConsts.graphDiscontinuity && (x + c_drawConsts.graphSampleXScale) >= c_drawConsts.graphDiscontinuity)
        return false;

    if (x >= c_drawConsts.graphDiscontinuity && (x - c_drawConsts.graphSampleXScale) < c_drawConsts.graphDiscontinuity)
        return false;

    return true;
}


float4 GetGraphColour(uint colourIdx, uint selectedInst, float x, uint instId, int s1, int s2)
{
    float4 colour = g_graphColours[colourIdx];
    
    // If s1 == s2 or crossing discontinuity then alpha out.
    colour.a = CheckDiscontinuity(x) * (s1 != s2);
    if (instId != selectedInst)
        colour.a *= 0.15;
        
    return colour;
}

// Pass y scaled to [0,1] range for viewport
float4 GetGraphPoint(uint idx, float y)
{
    return float4(idx * c_drawConsts.graphViewportScale - 1, 2 * y - 1, 0, 1);
}
