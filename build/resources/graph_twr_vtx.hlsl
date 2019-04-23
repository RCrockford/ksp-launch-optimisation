#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float x = GetGraphSample(vertexId);

    uint s1, s2;
    float f = GetTelemetrySamples(x, instId, s1, s2);

    float h = length(lerp(g_TelemetryData[s1].eciPosition, g_TelemetryData[s2].eciPosition, f)) - Re;
    uint stage = uint(lerp(g_TelemetryData[s1].stage, g_TelemetryData[s2].stage, f) + 0.5);
    StageData stageData = c_missionParams.stage[stage];
    
    float thrust = stageData.massFlow * g0 * lerp(stageData.IspVac, stageData.IspSL, getStaticPressure(h) / getStaticPressure(0));
    float mass = length(lerp(g_TelemetryData[s1].mass, g_TelemetryData[s2].mass, f));

    float r = h + Re;
    float g = mu / sqr(r);
    
    float twr = thrust / (mass * g);

    PSInput output;
    output.position = GetGraphPoint(vertexId, twr > 1e-5 ? log10(twr) * 0.75 + 0.25 : 0);
    output.colour = GetGraphColour(GraphColour_TWR, 0, x, instId, s1, s2);

    return output;
}
