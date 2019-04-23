#include "shader_resources.h"
#include "data_formats.h"
#include "graph_draw_vtx.hlsl"

PSInput main(uint vertexId : SV_VertexID, uint instId : SV_InstanceID)
{
    float2 p = float2(1 + (vertexId & 1) * 2, 0);
    p.x *= (vertexId & 2) ? 1.75 : 2.5;
    
    float2x2 m;
    sincos((vertexId >> 1) * (1.570796327 * 0.5), m._m10, m._m00);
    m._m01 = -m._m10;
    m._m11 = m._m00;
    
    p = mul(m, p) * 0.01;
    p.y *= 0.5;
    
    float2 centre = float2(c_frameConsts.selectedData % c_ThreadWidth, c_frameConsts.selectedData / c_ThreadWidth);
    centre = clamp(centre, 0.1, float2(c_ThreadWidth-1.1, c_ThreadHeight-1.1));
    
    p += centre * rcp(float2(c_ThreadWidth-1, c_ThreadHeight-1)) * 2 - 1;
    
    PSInput output;
    output.position = float4(p, 0, 1);
    output.colour = c_frameConsts.selectionFlash ? float4(0, 0, 0, 1) : float4(0.9, 0.9, 0.9, 1);
    
    return output;
}
