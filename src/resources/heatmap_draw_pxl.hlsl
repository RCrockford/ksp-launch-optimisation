// Helper funcs for heatmap drawing

#include "colour_code.hlsl"
#include "environmental_data.hlsl"

StructuredBuffer<FlightData>    g_FlightData    : register(t[srvFlightData]);

ConstantBuffer<MissionParams>   c_missionParams : register(b[cbvMissionParams]);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float getOrbitVariance(int s)
{
    float targetPe = c_missionParams.finalState.r - Re;
    float targetAp = (-mu / (2 * c_missionParams.finalOrbitalEnergy) - Re) * 2 - targetPe;

    float Ap = (1 + g_FlightData[s].e) * g_FlightData[s].a - Re;
    float Pe = (1 - g_FlightData[s].e) * g_FlightData[s].a - Re;

    return sqrt(sqr(Ap - targetAp) + sqr(Pe - targetPe));
}

// Return float4(mass, maxQ, orbit_variance, valid)
float4 GetHeatmapData(float2 uv)
{
    // Sampling locations
    int4 s;
    float2 f = modf(saturate(uv) * uint2(c_ThreadWidth - 1, c_ThreadHeight - 1), s.xy);

    s.x = s.x + s.y * c_ThreadWidth;
    s.y = s.x + 1;
    s.z = s.x + c_ThreadWidth;
    s.w = s.x + c_ThreadWidth + 1;

    s = clamp(s, 0, c_FlightDataMin - 1);
    
    f = smoothstep(0.1, 0.9, f);

    float4 w;
    w.x = (1 - f.x) * (1 - f.y);
    w.y = f.x * (1 - f.y);
    w.z = (1 - f.x) * f.y;
    w.w = f.x * f.y;

    float4 data = float4(g_FlightData[s.x].minMass, g_FlightData[s.x].maxQ, getOrbitVariance(s.x), g_FlightData[s.x].flightPhase == c_PhaseMECO) * w.x;
    data += float4(g_FlightData[s.y].minMass, g_FlightData[s.y].maxQ, getOrbitVariance(s.y), g_FlightData[s.y].flightPhase == c_PhaseMECO) * w.y;
    data += float4(g_FlightData[s.z].minMass, g_FlightData[s.z].maxQ, getOrbitVariance(s.z), g_FlightData[s.z].flightPhase == c_PhaseMECO) * w.z;
    data += float4(g_FlightData[s.w].minMass, g_FlightData[s.w].maxQ, getOrbitVariance(s.w), g_FlightData[s.w].flightPhase == c_PhaseMECO) * w.w;

    data.w = sqr(data.w);

    float4 rangeMin = float4(g_FlightData[c_FlightDataMax].minMass * 0.96, 40000, 0, -0.25);
    float4 rangeMax = float4(g_FlightData[c_FlightDataMax].minMass, 60000, (c_missionParams.finalState.r - Re) * 0.025, 1.25);

    data = (data - rangeMin) / (rangeMax - rangeMin);

    return data;
}
