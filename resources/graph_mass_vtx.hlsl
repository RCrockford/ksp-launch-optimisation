#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float m = lerp(g_TelemetryData[s1].mass, g_TelemetryData[s2].mass, f);
	
    PSInput output;
    output.position = GetGraphPoint(vertexId, m / c_missionParams.stage[0].wetMass);
    output.colour = GetGraphColour(GraphColour_Mass, 0, x, instId, s1, s2);

    return output;
}
