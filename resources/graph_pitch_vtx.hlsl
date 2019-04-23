#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float2 h = normalize(lerp(g_TelemetryData[s1].heading, g_TelemetryData[s2].heading, f));
    float2 r = normalize(lerp(g_TelemetryData[s1].eciPosition, g_TelemetryData[s2].eciPosition, f));
    
	float pitch = c_Pi - acos(dot(h, r));

    PSInput output;
    output.position = GetGraphPoint(vertexId, pitch / c_Pi);
    output.colour = GetGraphColour(GraphColour_Pitch, 0, x, instId, s1, s2);

    return output;
}
