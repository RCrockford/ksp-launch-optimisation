#include "shader_resources.h"
#include "data_formats.h"

#include "heatmap_draw_pxl.hlsl"

float4 main(PSInput input) : SV_TARGET
{
    float4 w;
    int4 s;
    GetFlightDataBlend(input.uv, w, s);
    
    float maxV = g_FlightData[s.x].maxAltitude * w.x;
    maxV += g_FlightData[s.y].maxAltitude * w.y;
    maxV += g_FlightData[s.z].maxAltitude * w.z;
    maxV += g_FlightData[s.w].maxAltitude * w.w;
    
    float rangeMin = g_FlightData[c_FlightDataMin].maxAltitude;
    float rangeMax = g_FlightData[c_FlightDataMax].maxAltitude;
    
    float x = length(maxV);
    x = (x - rangeMin) / (rangeMax - rangeMin);

    return float4(ColourCodeRedGreen(x), 1);
}
