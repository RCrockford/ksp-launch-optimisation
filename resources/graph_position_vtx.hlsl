#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);
    
    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);
    
    float graphYScale = 1.0 / 2e5;  // Should be from data.
    
    float y = lerp(g_TelemetryData[s1].eciPosition.y, g_TelemetryData[s2].eciPosition.y, f);
    y *= graphYScale;
    
    PSInput output;
    output.position = GetGraphPoint(vertexId, y);
    output.colour = g_graphColours[0];
    
    output.position.x = lerp(g_TelemetryData[s1].eciPosition.x, g_TelemetryData[s2].eciPosition.x, f) * graphYScale - 1;
    
    // If s1 == s2 or crossing discontinuity then alpha out.
    output.colour.a = CheckDiscontinuity(x) * (s1 != s2);
    if (instId != 0)
        output.colour.a *= 0.1;
    
    return output;
}
