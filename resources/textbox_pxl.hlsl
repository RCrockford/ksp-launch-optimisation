#include "shader_resources.h"
#include "data_formats.h"
#include "colour_code.hlsl"

struct PSInput
{
	float4 position : SV_POSITION;
};


float4 main(PSInput input) : SV_TARGET
{
    return float4(0, 0, 0, 0.5);
}
