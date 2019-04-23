#include "shader_resources.h"
#include "data_formats.h"

Buffer<float4>      g_pressureHeightCurve       : register(t[srvPressureHeightCurve]);
Buffer<float4>      g_temperatureHeightCurve    : register(t[srvTemperatureHeightCurve]);
Buffer<float4>      g_liftMachCurve[4]          : register(t[srvLiftMachCurve]);
Buffer<float4>      g_dragMachCurve[4]          : register(t[srvDragMachCurve]);

// Everything is SI standard units.

cbuffer EnvironmentalData : register(b[cbvEnviroParams])
{
    // Gravitational constant
    float mu;
    // Earth radius
    float Re;
    // Standard gravity
    float g0;
    // Molar mass of air
    float airM;
    // Universal gas constant
    float Rstar;
    // Adiabatic index
    float airGamma;
    // Launch latitude
    float launchLatitude;
    // Launch altitude
    float launchAltitude;
    // Rotation period
    float rotationPeriod;
};

float evaluateHermiteCurve(Buffer<float4> curve, float x)
{
    if (x <= curve[0].x)
    {
        return curve[0].y;
    }

    uint len;
    curve.GetDimensions(len);
    --len;

    if (x >= curve[len].x)
    {
        return curve[len].y;
    }

    // Work out which segment we are in.
    uint i;
    for (i = 0; i < len; ++i)
    {
        if (x >= curve[i].x && x < curve[i+1].x)
            break;
    }

    // evaluate hermite
    float t = (x - curve[i].x) / (curve[i+1].x - curve[i].x);
    float t2 = sqr(t);
    float t3 = cube(t);

    return (2*t3 - 3*t2 + 1) * curve[i].y + (t3 - 2*t2 + t) * curve[i].w + (-2*t3 + 3*t2) * curve[i+1].y + (t3 - t2) * curve[i+1].z;
}

float getStaticPressure(float h)
{
    return evaluateHermiteCurve(g_pressureHeightCurve, h) * 1000;
}

float getTemperature(float h)
{
    return evaluateHermiteCurve(g_temperatureHeightCurve, h);
}

float getAtmosDensity(float P, float T)
{
    return P * airM / (Rstar * T);
}

float calcQfromDensity(float rho, float v)
{
    return 0.5 * rho * sqr(v);
}

float calcQfromPressure(float P, float M)
{
    return 0.5 * airGamma * P * sqr(M);
}

float getSpeedOfSound(float T)
{
    return sqrt(airGamma * Rstar * T / airM);
}

// Drag, M is mach number
float getCdA(uint stage, float M)
{
    return evaluateHermiteCurve(g_dragMachCurve[stage], M);
}

// Lift, M is mach number
float getClA(uint stage, float M)
{
    return evaluateHermiteCurve(g_liftMachCurve[stage], M) * 1e-3;
}

void CalcOrbitParameters(float2 eciPos, float2 eciVel, out float a, out float2 e, out float E)
{
    float v2 = dot(eciVel, eciVel);
    float r = length(eciPos);
    
    // Calc eccentricty
    e = (v2 / mu - 1 / r) * eciPos - (dot(eciPos, eciVel) / mu) * eciVel;
    
    // Calc semi-major axis
    E = v2 / 2 - mu / r;
    a = -mu / (2 * E);
}
