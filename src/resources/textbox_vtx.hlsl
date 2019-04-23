#include "shader_resources.h"
#include "data_formats.h"

struct PSInput
{
	float4 position : SV_POSITION;
};

PSInput main(uint vertexId : SV_VertexID)
{
	PSInput output;

    float2 uv = float2(vertexId & 1, vertexId >> 1) * c_drawConsts.viewportSize;
    output.position = float4(c_drawConsts.invViewportSize + uv, 0.0f, 1.0f);
    
	return output;
}
