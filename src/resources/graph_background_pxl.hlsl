#include "shader_resources.h"
#include "data_formats.h"
#include "colour_code.hlsl"

#include "graph_draw_vtx.hlsl"

struct BkgdPSInput
{
	float4 position : SV_POSITION;
 	float2 uv : TEXCOORD;
};

float4 main(BkgdPSInput input) : SV_TARGET
{
    float x = input.uv.x * c_drawConsts.graphSampleXScale;
    
    float d = (input.uv.x * 4) - floor(input.uv.x * 4 + 0.5);
    if (abs(d) < c_drawConsts.invViewportSize.x * 2)
        return float4(0.35,0.35,0.35,1);

    d = (input.uv.y * 4) - floor(input.uv.y * 4 + 0.5);
    if (abs(d) < c_drawConsts.invViewportSize.y * 2)
        return float4(0.35,0.35,0.35,1);

    uint s1, s2;
    float f = GetTelemetrySamples(x, 0, s1, s2);

    float stage = lerp(g_TelemetryData[s1].stage, g_TelemetryData[s2].stage, f);
    float phase = lerp(g_TelemetryData[s1].flightPhase, g_TelemetryData[s2].flightPhase, f);

    if (input.uv.y < 0.75)
        return float4(ColourCodeRedGreen(phase * 0.25).xzy * 0.5, 0.2);
    else
        return float4(ColourCodeRedGreen(stage * 0.25).yxz * 0.5, 0.2);
}
