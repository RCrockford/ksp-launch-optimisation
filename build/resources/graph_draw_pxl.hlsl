#include "shader_resources.h"
#include "data_formats.h"

struct PSInput
{
	float4 position : SV_POSITION;
    float4 colour : COLOUR;
};

float4 main(PSInput input) : SV_TARGET
{
    input.colour.a = 1 - sqr(1 - input.colour.a);
    return input.colour;
}
