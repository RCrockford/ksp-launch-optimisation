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
    float v = length(lerp(g_TelemetryData[s1].surfVelocity, g_TelemetryData[s2].surfVelocity, f));

    if (h < 0)
        v = 0;

    float P = getStaticPressure(h);
    float T = getTemperature(h);

    float M = v / getSpeedOfSound(T);
    float Q = calcQfromPressure(P, M);

    PSInput output;
    output.position = GetGraphPoint(vertexId, Q / 100000);
    output.colour = GetGraphColour(GraphColour_Q, 0, x, instId, s1, s2);

    return output;
}
