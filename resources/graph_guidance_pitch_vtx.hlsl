#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float pitch = lerp(g_TelemetryData[s1].guidancePitch, g_TelemetryData[s2].guidancePitch, f);

    PSInput output;
    output.position = GetGraphPoint(vertexId, pitch / c_Pi);
    output.colour = GetGraphColour(GraphColour_GuidancePitch, 0, x, instId, s1, s2);
    
    float x0 = GetGraphSample(max(vertexId, 1) - 1);
    uint s0;
    GetTelemetrySamples(x0, instId, s0, s1);
    output.colour.a *= (g_TelemetryData[s0].guidancePitch <= c_Pi) && (g_TelemetryData[s1].guidancePitch <= c_Pi) && (g_TelemetryData[s2].guidancePitch <= c_Pi);

    return output;
}
