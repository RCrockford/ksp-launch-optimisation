#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);
    
    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);
    
    float graphYScale = 1.0 / 11000;    // Just shy of escape velocity
    
    float y = length(lerp(g_TelemetryData[s1].surfVelocity, g_TelemetryData[s2].surfVelocity, f));
    y *= graphYScale;
    
    PSInput output;
    output.position = GetGraphPoint(vertexId, y);
    output.colour = GetGraphColour(GraphColour_Velocity, 0, x, instId, s1, s2);
    
    return output;
}
