#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float2 v = normalize(lerp(g_TelemetryData[s1].surfVelocity, g_TelemetryData[s2].surfVelocity, f));
	float2 h = normalize(lerp(g_TelemetryData[s1].heading, g_TelemetryData[s2].heading, f));
	
	float aoa = acos(min(dot(v, h), 1)) * c_RadToDegree;

    PSInput output;
    output.position = GetGraphPoint(vertexId, aoa / 18 + 0.5);
    output.colour = GetGraphColour(GraphColour_AoA, 0, x, instId, s1, s2);

    return output;
}
