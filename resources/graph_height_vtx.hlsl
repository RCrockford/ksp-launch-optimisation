#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);
    
    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);
    
    float graphYScale = 0.8 / (c_missionParams.finalState.r - Re);
    
    float h = length(lerp(g_TelemetryData[s1].eciPosition, g_TelemetryData[s2].eciPosition, f)) - Re;
    h *= graphYScale;
    
    PSInput output;
    output.position = GetGraphPoint(vertexId, h);
    output.colour = GetGraphColour(GraphColour_Height, 0, x, instId, s1, s2);

    return output;
}
