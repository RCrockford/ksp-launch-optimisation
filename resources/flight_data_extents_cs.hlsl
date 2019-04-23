#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"

RWStructuredBuffer<FlightData>  g_FlightData    : register(u[uavFlightData]);

ConstantBuffer<MissionParams>   c_missionParams : register(b[cbvMissionParams]);

[numthreads(1, 1, 1)]
void main(uint threadIdx : SV_GroupIndex)
{
    // Update flight data extents
    FlightData minData, maxData;

    bool dataValid = false;
    minData.flightPhase = ~0;
    maxData.flightPhase = 0;
        
    // Pass 1, determine flight phase extents
    for (uint i = 0; i < c_FlightDataMin; ++i)
    {
        minData.flightPhase = min(minData.flightPhase, g_FlightData[i].flightPhase);
        maxData.flightPhase = max(maxData.flightPhase, g_FlightData[i].flightPhase);
    }
    
    // Pass 2, extents only captured for flights reaching maximum flight phase
    for (i = 0; i < c_FlightDataMin; ++i)
    {
        if (g_FlightData[i].flightPhase == maxData.flightPhase)
        {
            // Ignore flights which missed the target orbit significantly
            if (abs((1 - g_FlightData[i].e) * g_FlightData[i].a - c_missionParams.finalState.r) < (c_missionParams.finalState.r - Re) * 0.03)
            {
                if (!dataValid)
                {
                    uint minFlightPhase = minData.flightPhase;
                
                    minData = g_FlightData[i];
                    maxData = g_FlightData[i];
                    dataValid = true;
                    
                    minData.flightPhase = minFlightPhase;
                }
                else
                {
                    minData.maxAltitude = min(minData.maxAltitude, g_FlightData[i].maxAltitude);
                    maxData.maxAltitude = max(maxData.maxAltitude, g_FlightData[i].maxAltitude);
                    minData.maxSurfSpeed = min(minData.maxSurfSpeed, g_FlightData[i].maxSurfSpeed);
                    maxData.maxSurfSpeed = max(maxData.maxSurfSpeed, g_FlightData[i].maxSurfSpeed);
                    minData.maxEciSpeed = min(minData.maxEciSpeed, g_FlightData[i].maxEciSpeed);
                    maxData.maxEciSpeed = max(maxData.maxEciSpeed, g_FlightData[i].maxEciSpeed);
                    minData.maxQ = min(minData.maxQ, g_FlightData[i].maxQ);
                    maxData.maxQ = max(maxData.maxQ, g_FlightData[i].maxQ);
                    minData.minMass = min(minData.minMass, g_FlightData[i].minMass);
                    maxData.minMass = max(maxData.minMass, g_FlightData[i].minMass);
                    minData.maxAccel = min(minData.maxAccel, g_FlightData[i].maxAccel);
                    maxData.maxAccel = max(maxData.maxAccel, g_FlightData[i].maxAccel);
                }
            }
        }
    }
    
    g_FlightData[c_FlightDataMin] = minData;
    g_FlightData[c_FlightDataMax] = maxData;
}
