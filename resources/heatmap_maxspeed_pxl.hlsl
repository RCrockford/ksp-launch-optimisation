#include "shader_resources.h"
#include "data_formats.h"

#include "heatmap_draw_pxl.hlsl"

float4 main(PSInput input) : SV_TARGET
{
    float4 w;
    int4 s;
    GetFlightDataBlend(input.uv, w, s);
    
    float maxV = g_FlightData[s.x].maxSurfSpeed * w.x;
    maxV += g_FlightData[s.y].maxSurfSpeed * w.y;
    maxV += g_FlightData[s.z].maxSurfSpeed * w.z;
    maxV += g_FlightData[s.w].maxSurfSpeed * w.w;
    
    float rangeMin = g_FlightData[c_FlightDataMin].maxSurfSpeed;
    float rangeMax = g_FlightData[c_FlightDataMax].maxSurfSpeed;
    
    float x = length(maxV);
    x = (x - rangeMin) / (rangeMax - rangeMin);

    return float4(ColourCodeRedGreen(x), 1);
}
