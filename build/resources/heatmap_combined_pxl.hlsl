#include "shader_resources.h"
#include "data_formats.h"

#include "heatmap_draw_pxl.hlsl"


float4 main(PSInput input) : SV_TARGET
{
    float4 data = GetHeatmapData(input.uv);
    
    float combined = saturate(data.x) * sqrt(saturate(1 - data.y)) * saturate(1 - data.z);

    return float4(ColourCodeRedGreen(combined) * data.w, 1);
}
