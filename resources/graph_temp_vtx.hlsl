#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    uint x = GetGraphSample(vertexId);

    float M = float(x) / 16000.0 + 0.4;
    float y = getCdA(instId, M)* 5;
    
//    float y = getCdA(x / 28000.0);
    
    PSInput output;
    output.position = GetGraphPoint(vertexId, y);
    output.colour = g_graphColours[instId];
    output.colour.a = 0.5;
    
    return output;
}
