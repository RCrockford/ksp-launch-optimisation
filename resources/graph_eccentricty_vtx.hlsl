#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float2 eciPos = lerp(g_TelemetryData[s1].eciPosition, g_TelemetryData[s2].eciPosition, f);
    float2 eciVel = lerp(g_TelemetryData[s1].eciVelocity, g_TelemetryData[s2].eciVelocity, f);
    
    float2 e;
    float a, E;    
    CalcOrbitParameters(eciPos, eciVel, a, e, E);

    PSInput output;
    output.position = GetGraphPoint(vertexId, length(e));
    output.colour = GetGraphColour(GraphColour_Eccentricty, 0, x, instId, s1, s2);

    return output;
}
