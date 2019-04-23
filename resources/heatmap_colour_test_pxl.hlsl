#include "shader_resources.h"
#include "data_formats.h"

#include "heatmap_draw_pxl.hlsl"

float3 LabTosRGB(float3 Lab)
{
    float3 fLab = (Lab + float3(16,0,0)) / float3(116.0, 500.0, 200.0);
    
    float3 fXYZ = mad(fLab.yzz, float3(1, 0, -1), fLab.xxx);
    
    float3 XYZd65 = (fXYZ <= 6.0 / 29.0) ? (fXYZ - 4.0 / 29.0) * (3.0 * 36.0 / 841.0) : pow(fXYZ, 3.0);
    XYZd65 *= float3 ( 0.95047, 1.0, 1.08883 );
    
    float3 linRGB;
    linRGB.x = dot(XYZd65, float3(3.2406, -1.5372, -0.4986));
    linRGB.y = dot(XYZd65, float3(-0.9689, 1.8758, -0.0415));
    linRGB.z = dot(XYZd65, float3(0.0557, -0.2040, 1.0570));

    float3 sRGB = ( linRGB < 0.0031308 ) ? (linRGB * 12.92) : mad( pow( abs( linRGB ), 1.0 / 2.4 ), 1.055f, -0.055f );
    
    return sRGB;
}

float4 main(PSInput input) : SV_TARGET
{
    float4 output;
    
    uint2 sector = min(uint2(input.uv * 4), 3);
    
    float3 Lab;
    Lab.yz = float2(sector & 1) * 120 - 60;
    uint x = (sector.x & 1) + (sector.y & 1) * 2;
    
    sector >>= 1;
    uint s = sector.x + sector.y * 2;
    
    float r;
    if (s == 0)
    {
        Lab.x = 48 - x * 2;
        r = -0.05;
    }
    else if (s == 2)
    {
        Lab.x = 35;
        r = 0.1;
        Lab.yz *= 0.4;
    }
    else if (s == 1)
    {
        Lab.x = 80;
        r = -0.45;
    }
    else if (s == 3)
    {
        Lab.x = 55;
        r = -0.95;
        Lab.yz *= 0.25;
    }
    
    float2 sc;
    sincos(r, sc.x, sc.y);
    
    Lab.yz = Lab.yz * sc.yy + Lab.zy * sc.xx * float2(-1,1);
    
    output.rgb = LabTosRGB(Lab);
    output.a = 1;
    
    return output;
}
