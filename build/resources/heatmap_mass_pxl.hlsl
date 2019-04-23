#include "shader_resources.h"
#include "data_formats.h"

#include "heatmap_draw_pxl.hlsl"

float4 main(PSInput input) : SV_TARGET
{
    float4 data = GetHeatmapData(input.uv);

    return float4(ColourCodeRedGreen(sqr(saturate(data.x))) * data.w, 1);
}
