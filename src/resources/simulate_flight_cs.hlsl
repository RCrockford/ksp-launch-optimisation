#include "shader_resources.h"
#include "data_formats.h"
#include "environmental_data.hlsl"

RWStructuredBuffer<TelemetryData>   g_TelemetryData : register(u[uavTelemetryData]);
RWStructuredBuffer<FlightData>      g_FlightData    : register(u[uavFlightData]);

StructuredBuffer<AscentParams>      g_ascentParams  : register(t[srvAscentParams]);

ConstantBuffer<MissionParams>       c_missionParams : register(b[cbvMissionParams]);

static const uint c_debugThread = 296;

//------------------------------------------------------------------------------------------------

struct FlightIntegrals
{
    float   b0;     // delta v
    float   b1;     // first moment of b0
    float   c0;     // ideal distance travelled
    float   c1;     // first moment of c0
};

struct HeadingDerivatives
{
    float r;        // radial heading
    float rT;       // radial heading at T
    float dr;       // first derivative of radial heading

    float h;        // crosstrack heading
    float hT;       // crosstrack heading at T
    float dh;       // first derivative of crosstrack heading

    float theta;    // downtrack heading
    float dtheta;   // first derivative of downtrack heading
    float ddtheta;  // second derivative of downtrack heading
};

//------------------------------------------------------------------------------------------------

float2 solveGuidance(float2x2 Ma, float2 Mb)
{
    float det = determinant(Ma);
    if (abs(det) > 1e-7)
    {
        float2 Mx;
        Mx.x = (Ma._m11 * Mb.x - Ma._m01 * Mb.y) / det;
        Mx.y = (Ma._m00 * Mb.y - Ma._m10 * Mb.x) / det;

        return Mx;
    }

    return float2(0, 0);
}

FlightIntegrals calcFlightIntegrals(float exhaustV, float tau, float T)
{
    FlightIntegrals integrals;
    integrals.b0 = -exhaustV * log(1 - T / tau); // delta V
    integrals.b1 = integrals.b0 * tau - exhaustV * T;
    integrals.c0 = integrals.b0 * T - integrals.b1;
    integrals.c1 = integrals.c0 * tau - exhaustV * sqr(T) * 0.5;

    return integrals;
}

ReferenceFrame calcReferenceFrame(float3 position, float3 velocity)
{
    ReferenceFrame rf;
    rf.r = length(position);
    rf.h = length(cross(position, velocity));
    rf.omega = rf.h / sqr(rf.r);
    rf.rv = dot(velocity, position / rf.r);

    return rf;
}

HeadingDerivatives calcHeadingDerivatives(GuidanceData G, ReferenceFrame rf, ReferenceFrame S, float accel, float accelT)
{
    HeadingDerivatives f;

    f.r = G.A + (mu / sqr(rf.r) - sqr(rf.omega) * rf.r) / accel;
    f.rT = G.A + G.B * G.T + (mu / sqr(S.r) - sqr(S.omega) * S.r) / accelT;
    f.dr = (f.rT - f.r) / G.T;

    f.h = 0;
    f.hT = 0;
    f.dh = 0;

    f.theta = 1 - sqr(f.r) * 0.5 - sqr(f.h) * 0.5;
    f.dtheta = -(f.r * f.dr + f.h * f.dh);
    f.ddtheta = -(sqr(f.dr) + sqr(f.dh)) * 0.5;

    return f;
}

//------------------------------------------------------------------------------------------------

// Low frequency guidance loop, does estimation and updates current guidance.
void updateGuidanceFinalStage(inout GuidanceData G, ReferenceFrame rf, float exhaustV, float accel)
{
    // Step steering constants forward
    G.A += G.B * G.t;
    G.T -= G.t;

    float tau = exhaustV / accel;
    float accelT = accel / (1 - G.T / tau);

    const ReferenceFrame S = c_missionParams.finalState;
    HeadingDerivatives f = calcHeadingDerivatives(G, rf, S, accel, accelT);

    // Calculate required delta V
    float dh = S.h - rf.h;
    float meanRadius = (rf.r + S.r) * 0.5;

    float deltaV = dh / meanRadius;
    deltaV += exhaustV * G.T * (f.dtheta + f.ddtheta * tau);
    deltaV += f.ddtheta * exhaustV * sqr(G.T) * 0.5;
    deltaV /= f.theta + (f.dtheta + f.ddtheta * tau) * tau;

    // Calculate new estimate for T
    G.T = tau * (1 - exp(-deltaV / exhaustV));
    G.t = 0;

    // Update A, B with new T estimate
    FlightIntegrals integrals = calcFlightIntegrals(exhaustV, tau, G.T);

    float2 AB = solveGuidance(float2x2( integrals.b0, integrals.b1, integrals.c0, integrals.c1 ), float2(S.rv - rf.rv, S.r - rf.r - rf.rv * G.T) );
    G.A = AB.x;
    G.B = AB.y;
}

//------------------------------------------------------------------------------------------------

// Low frequency guidance loop, does estimation and updates current guidance.
void updateGuidanceInterStage(inout GuidanceData G, inout GuidanceData G2, inout ReferenceFrame rf, float2 exhaustV, float2 accel)
{
    // Step steering constants forward
    G.A += G.B * G.t;
    G.T -= G.t;
    G.T = max(G.T, 1);  // Must always be > 0 while stage active

    float tau = exhaustV.x / accel.x;
    float accelT = accel.x / (1 - G.T / tau);

    // Current flight integrals
    FlightIntegrals fi = calcFlightIntegrals(exhaustV.x, tau, G.T);

    // state at staging
    ReferenceFrame S;
    S.r = rf.r + rf.rv * G.T + fi.c0 * G.A + fi.c1 * G.B;
    S.rv = rf.rv + fi.b0 * G.A + fi.b1 * G.B;
    S.h = 0;
    S.omega = G.omegaT;

    HeadingDerivatives f = calcHeadingDerivatives(G, rf, S, accel.x, accelT);

    // Angular momentum gain at staging
    float b2 = fi.b1 * tau - exhaustV.x * sqr(G.T) * 0.5;
    S.h = rf.h + (rf.r + S.r) * 0.5 * (f.theta * fi.b0 + f.dtheta * fi.b1 + f.ddtheta * b2);

    // Tangental and angular speed at staging
    float VtangT = S.h / S.r;
    S.omega = VtangT / S.r;
    G.omegaT = S.omega;  // feedback to next loop

    // guidance discontinuities at staging.
    float x = (mu / sqr(S.r) - sqr(S.omega) * S.r);
    float deltaA = x * (1 / accelT - 1 / accel.y);
    float deltaB = -x * (1 / exhaustV.x - 1 / exhaustV.y) + (3 * sqr(S.omega) - 2 * mu / cube(S.r)) * S.rv * (1 / accelT - 1 / accel.y);

    // Next stage flight integrals
    float tau2 = exhaustV.y / accel.y;
    float accelT2 = accel.y / (1 - G2.T / tau2);

    FlightIntegrals fi2 = calcFlightIntegrals(exhaustV.y, tau2, G2.T);

    // Update guidance for current stage
    float2x2 Ma;
    Ma._m00 = fi.b0 + fi2.b0;
    Ma._m01 = fi.b1 + fi2.b1 + fi2.b0 * G.T;
    Ma._m10 = fi.c0 + fi2.c0 + fi.b0 * G2.T;
    Ma._m11 = fi.c1 + fi.b1 * G2.T + fi2.c0 * G.T + fi2.c1;

    ReferenceFrame S2;
    if (G2.omegaT < 0)
    {
        S2 = c_missionParams.finalState;
    }
    else
    {
        S2.r = S.r + S.rv * G2.T + fi2.c0 * G2.A + fi2.c1 * G2.B;
        S2.rv = S.rv + fi2.b0 * G2.A + fi2.b1 * G2.B;
    }

    float2 Mb;
    Mb.x = S2.rv - rf.rv - fi2.b0 * deltaA - fi2.b1 * deltaB;
    Mb.y = S2.r - rf.r - rf.rv * (G.T + G2.T) - fi2.c0 * deltaA - fi2.c1 * deltaB;

    float2 AB = solveGuidance(Ma, Mb);
    G.A = AB.x;
    G.B = AB.y;
    G.t = 0;

    // Update next stage guidance using staging state at start
    G2.A = deltaA + AB.x + AB.y * G.T;
    G2.B = deltaB + AB.y;

    // Update reference frame for next stage
    rf = S;
}

//------------------------------------------------------------------------------------------------

// Multi-stage powered explicit guidance
void updateGuidance(uint threadIdx, uint stage, float3 position, float3 velocity, float exhaustV, float accel)
{
    float2 stageEv = float2(exhaustV, 0);
    float2 stageAccel = float2(accel, 0);

    ReferenceFrame rf = calcReferenceFrame(position, velocity);

    for (uint s = stage; s < c_missionParams.stageCount; ++s)
    {
        if (s < c_missionParams.stageCount - 1)
        {
            // Assume next stage will be running in vacuum.
            stageEv.y = g0 * c_missionParams.stage[s+1].IspVac;
            stageAccel.y = c_missionParams.stage[s+1].massFlow * stageEv.y / c_missionParams.stage[s+1].wetMass;

            updateGuidanceInterStage(g_FlightData[threadIdx].guidance[s], g_FlightData[threadIdx].guidance[s+1], rf, stageEv, stageAccel);

            stageEv.x = stageEv.y;
            stageAccel.x = stageAccel.y;
        }
        else
        {
            updateGuidanceFinalStage(g_FlightData[threadIdx].guidance[s], rf, stageEv.x, stageAccel.x);
        }
    }
}

void convergeGuidance(uint threadIdx, uint stage, float3 position, float3 velocity, float exhaustV, float accel)
{
    float4 allStageEv = float4(exhaustV, 0, 0, 0);
    float4 allStageAccel = float4(accel, 0, 0, 0);
    ReferenceFrame currentRF = calcReferenceFrame(position, velocity);
    
    for (uint i = 1; i < 4; ++i)
    {
        if (c_missionParams.stage[stage+i].wetMass > 0)
        {
            allStageEv[i] = g0 * c_missionParams.stage[stage+i].IspVac;
            allStageAccel[i] = c_missionParams.stage[stage+i].massFlow * allStageEv[i] / c_missionParams.stage[stage+i].wetMass;
        }
    }
    
    uint convergedStages = stage;
    for (uint count = 0; convergedStages < c_missionParams.stageCount && count < 30; ++count)
    {
        float A = g_FlightData[threadIdx].guidance[convergedStages].A;
        
        float4 stageEv = allStageEv;
        float4 stageAccel = allStageAccel;
        ReferenceFrame rf = currentRF;
        
        for (uint s = stage; s <= convergedStages; ++s)
        {
            if (s < c_missionParams.stageCount - 1)
            {
                updateGuidanceInterStage(g_FlightData[threadIdx].guidance[s], g_FlightData[threadIdx].guidance[s+1], rf, stageEv.xy, stageAccel.xy);

                stageEv.xyz = stageEv.yzw;
                stageAccel.xyz = stageAccel.yzw;
            }
            else
            {
                updateGuidanceFinalStage(g_FlightData[threadIdx].guidance[s], rf, stageEv.x, stageAccel.x);
            }
        }
        
        if (abs(A - g_FlightData[threadIdx].guidance[convergedStages].A) < 0.01)
            ++convergedStages;
    }
}

//------------------------------------------------------------------------------------------------

// High frequency guidance loop, does steering control.
float3 getGuidanceAim(inout GuidanceData G, float3 position, float3 velocity, float accel)
{
    float r = length(position);
    float3 radial = position / r;
    float3 downtrack = cross(normalize(cross(position, velocity)), radial);
    float omega = dot(velocity, downtrack) / r;

    // Calculate radial heading vector
    float Fr = G.A + G.B * G.t;
    // Add gravity and centifugal force term.
    Fr += (mu / sqr(r) - sqr(omega) * r) / accel;

    // Construct vector
    float3 aim = float3(0,0,0);
    if (Fr < 1)
    {
        aim = Fr * radial + sqrt(1 - sqr(Fr)) * downtrack;
    }

    // Advance t
    G.t += c_frameConsts.timeStep;

    return aim;
}

//------------------------------------------------------------------------------------------------

[numthreads(c_ThreadWidth, c_ThreadHeight, 1)]
void main(uint threadIdx : SV_GroupIndex)
{
    uint dataIdx = c_dispatchConsts.dstDataOffset + threadIdx;

    // Roll last frame state forward
    if (c_dispatchConsts.srcDataOffset != c_dispatchConsts.dstDataOffset)
    {
        if (c_dispatchConsts.srcDataOffset < c_dispatchConsts.dstDataOffset)
        {
            g_TelemetryData[dataIdx] = g_TelemetryData[c_dispatchConsts.srcDataOffset + threadIdx];
        }
        else
        {
            // Initial state
            g_TelemetryData[dataIdx].eciPosition = float2(0, Re + launchAltitude);
            g_TelemetryData[dataIdx].surfVelocity = float2(0, 0);
            g_TelemetryData[dataIdx].heading = float2(0, 1);
            g_TelemetryData[dataIdx].stage = 0;
            g_TelemetryData[dataIdx].mass = c_missionParams.stage[0].wetMass;
            g_TelemetryData[dataIdx].guidancePitch = 4.0f;  // >pi == no guidance

            float padRadius = length(g_TelemetryData[dataIdx].eciPosition) * cos(launchLatitude * c_DegreeToRad);
            float padSpeed = 2 * c_Pi * padRadius / rotationPeriod;
            g_TelemetryData[dataIdx].eciVelocity = g_TelemetryData[dataIdx].surfVelocity + float2(padSpeed, 0);

            g_FlightData[threadIdx].maxAltitude = 0;
            g_FlightData[threadIdx].maxSurfSpeed = 0;
            g_FlightData[threadIdx].maxEciSpeed = 0;
            g_FlightData[threadIdx].maxQ = 1e-6;    // non-zero to avoid divide by zero in the aero flight calcs
            g_FlightData[threadIdx].minMass = g_TelemetryData[dataIdx].mass;
            g_FlightData[threadIdx].flightPhase = c_PhaseLiftoff;

            for (uint i = 0; i < c_missionParams.stageCount; ++i)
            {
                g_FlightData[threadIdx].guidance[i].A = 0;
                g_FlightData[threadIdx].guidance[i].B = 0;
                // Estimated burn time
                g_FlightData[threadIdx].guidance[i].T = (c_missionParams.stage[i].wetMass - c_missionParams.stage[i].dryMass) / c_missionParams.stage[i].massFlow;
                g_FlightData[threadIdx].guidance[i].t = 0;
                g_FlightData[threadIdx].guidance[i].omegaT = 0;
                
                g_FlightData[threadIdx].stageBurnTime[i] = 0;
            }

            // Flag for last stage
            g_FlightData[threadIdx].guidance[c_missionParams.stageCount-1].omegaT = -1;

            // If pitchover speed is 0, pitch on pad.
            if (g_ascentParams[threadIdx].pitchOverSpeed <= 0.0)
            {
                g_TelemetryData[dataIdx].heading = float2(g_ascentParams[threadIdx].sinPitchOverAngle, g_ascentParams[threadIdx].cosPitchOverAngle);
                g_FlightData[threadIdx].flightPhase = c_PhaseAeroFlight;
            }
        }
    }
    
    g_TelemetryData[dataIdx].T.x = g_FlightData[threadIdx].guidance[0].T - g_FlightData[threadIdx].guidance[0].t;
    g_TelemetryData[dataIdx].T.y = g_FlightData[threadIdx].guidance[1].T - g_FlightData[threadIdx].guidance[1].t;
    g_TelemetryData[dataIdx].T.z = g_FlightData[threadIdx].guidance[2].T - g_FlightData[threadIdx].guidance[2].t;
    g_TelemetryData[dataIdx].T.w = g_FlightData[threadIdx].guidance[3].T - g_FlightData[threadIdx].guidance[3].t;

    float2 upVec = normalize(g_TelemetryData[dataIdx].eciPosition);
    float h = max(length(g_TelemetryData[dataIdx].eciPosition) - Re, 0);

    // Crash = freeze telemetry
    if (h < 0)
    {
        return;
    }

    AscentParams ascentParams = g_ascentParams[threadIdx];

    StageData stageData = c_missionParams.stage[g_TelemetryData[dataIdx].stage];
    uint stage = g_TelemetryData[dataIdx].stage;

    // environmental data
    float P = getStaticPressure(h);
    float T = getTemperature(h);

    // calculate fuel burn time
    float mass = g_TelemetryData[dataIdx].mass;
    float fuelT = 0;
    if (g_FlightData[threadIdx].flightPhase < c_PhaseMECO)
    {
        fuelT = min((mass - stageData.dryMass) / (c_frameConsts.timeStep * stageData.massFlow), 1) * c_frameConsts.timeStep;
    }

    // calculate current thrust, F0
    float thrustMul = fuelT / c_frameConsts.timeStep;
    float exhaustV = g0 * lerp(stageData.IspVac, stageData.IspSL, P / getStaticPressure(0));
    float thrust = thrustMul * stageData.massFlow * exhaustV;

    // reduce thrust by drag
    float M = length(g_TelemetryData[dataIdx].surfVelocity) / getSpeedOfSound(T);
    float Q = calcQfromPressure(P, M);
    float Fdrag = Q * getCdA(stage, M);

    float2 position = g_TelemetryData[dataIdx].eciPosition;

    // Symplectic Euler integration; a0 = F0/m0, v1 = v0 + a0*dt, x1 = x0 + v1*dt
    // acceleration
    float2 acceleration = -normalize(position) * mu / dot(position, position);

    acceleration += ((thrust - Fdrag) * g_TelemetryData[dataIdx].heading) / mass;

    // velocity
    g_TelemetryData[dataIdx].surfVelocity += acceleration * c_frameConsts.timeStep;
    g_TelemetryData[dataIdx].eciVelocity += acceleration * c_frameConsts.timeStep;

    // position
    g_TelemetryData[dataIdx].eciPosition += g_TelemetryData[dataIdx].eciVelocity * c_frameConsts.timeStep;

    // Current osculating orbit
    float prevE = g_FlightData[threadIdx].E;
    float2 eccentricty;
    CalcOrbitParameters(g_TelemetryData[dataIdx].eciPosition, g_TelemetryData[dataIdx].eciVelocity, g_FlightData[threadIdx].a, eccentricty, g_FlightData[threadIdx].E);
    g_FlightData[threadIdx].e = length(eccentricty);

    // steering
    float2 aim;

    if (g_FlightData[threadIdx].flightPhase == c_PhaseLiftoff)
    {
        // if below pitch over speed just aim straight up.
        aim = upVec;
        if (length(g_TelemetryData[dataIdx].surfVelocity) >= ascentParams.pitchOverSpeed)
        {
            g_FlightData[threadIdx].flightPhase = c_PhasePitchOver;
        }
    }
    else if (g_FlightData[threadIdx].flightPhase == c_PhasePitchOver)
    {
        aim.x = dot(upVec, float2(ascentParams.cosAimAngle, ascentParams.sinAimAngle));
        aim.y = dot(upVec, float2(-ascentParams.sinAimAngle, ascentParams.cosAimAngle));

        float2 prograde = normalize(g_TelemetryData[dataIdx].surfVelocity);
        float pitch = dot(prograde, upVec);
        if (pitch <= ascentParams.cosPitchOverAngle)
        {
            g_FlightData[threadIdx].flightPhase = c_PhaseAeroFlight;
        }
    }
    else if (g_FlightData[threadIdx].flightPhase < c_PhaseMECO && thrust > 0)
    {
        float3 guidance;
        
        if (g_FlightData[threadIdx].flightPhase == c_PhaseAeroFlight)
        {
            if (Q <= g_FlightData[threadIdx].maxQ * 0.2)
            {
                // Update estimate for T.
                g_FlightData[threadIdx].guidance[stage].T = (mass - stageData.dryMass) / stageData.massFlow;
            
                convergeGuidance(threadIdx, stage, float3(g_TelemetryData[dataIdx].eciPosition, 0), float3(g_TelemetryData[dataIdx].eciVelocity, 0), exhaustV, thrust / mass);
                
                g_FlightData[threadIdx].flightPhase = c_PhaseGuidanceReady;
            }
            guidance = float3(0,0,0);
        }
        else
        {
            // Guidance runs every second
            if (g_FlightData[threadIdx].guidance[stage].t >= (1.0 - c_frameConsts.timeStep * 0.5) && g_FlightData[threadIdx].guidance[stage].T > 10)
            {
                updateGuidance(threadIdx, stage, float3(g_TelemetryData[dataIdx].eciPosition, 0), float3(g_TelemetryData[dataIdx].eciVelocity, 0), exhaustV, thrust / mass);
            }

            guidance = getGuidanceAim(g_FlightData[threadIdx].guidance[stage], float3(g_TelemetryData[dataIdx].eciPosition, 0), float3(g_TelemetryData[dataIdx].eciVelocity, 0), thrust / mass);
        }

        // If guidance is valid then it is a unit vector
        bool guidanceValid = dot(guidance, guidance) > 0.9;

        if (guidanceValid)
            g_TelemetryData[dataIdx].guidancePitch = c_Pi - acos(dot(upVec, guidance.xy));

        // If we don't have valid guidance then fall back to open loop
        if (g_FlightData[threadIdx].flightPhase < c_PhaseGuidanceActive || !guidanceValid)
        {
            aim = normalize(g_TelemetryData[dataIdx].surfVelocity);

            // Make sure dynamic pressure is low enough to start manoeuvres
            if (guidanceValid && Q <= g_FlightData[threadIdx].maxQ * 0.05)
            {
                // Check guidance pitch, when guidance is saying pitch down relative to open loop, engage guidance.
                // Alternatively, if Q is at 1% of maxQ, engage guidance.
                if (dot(upVec, guidance.xy) <= dot(upVec, aim) || (stage > 0 && Q <= g_FlightData[threadIdx].maxQ * 0.01))
                {
                    g_FlightData[threadIdx].flightPhase = c_PhaseGuidanceActive;
                    aim = guidance.xy;
                }
            }
        }
        else
        {
            aim = guidance.xy;

            // Have we reached final orbital energy, or likely to reach it within half the next time step?
            float deltaE = g_FlightData[threadIdx].E - prevE;
            if (g_FlightData[threadIdx].E >= c_missionParams.finalOrbitalEnergy ||
                (g_FlightData[threadIdx].E + deltaE * 0.5) >= c_missionParams.finalOrbitalEnergy)
            {
                g_FlightData[threadIdx].flightPhase = c_PhaseMECO;
            }
        }
    }
    else
    {
        aim = g_TelemetryData[dataIdx].heading;
    }

    // steer to aim (a bit rough but eh).
    float steerAngle = acos(dot(aim, g_TelemetryData[dataIdx].heading));
    float rotRate = max(stageData.rotationRate, 0.001) * c_frameConsts.timeStep * sqrt(c_missionParams.stage[0].wetMass / mass);
    g_TelemetryData[dataIdx].heading = normalize(lerp(g_TelemetryData[dataIdx].heading, aim, rotRate / max(steerAngle, rotRate)));

    // mass and staging
    g_TelemetryData[dataIdx].mass -= stageData.massFlow * fuelT;
    g_FlightData[threadIdx].stageBurnTime[stage] += (fuelT > 0) * c_frameConsts.timeStep;

    if (g_TelemetryData[dataIdx].mass <= stageData.dryMass)
    {
        uint nextStage = stage + 1;
        if (nextStage < c_missionParams.stageCount)
        {
            g_TelemetryData[dataIdx].stage = nextStage;
            g_TelemetryData[dataIdx].mass = c_missionParams.stage[nextStage].wetMass;
        }
    }

    g_TelemetryData[dataIdx].flightPhase = g_FlightData[threadIdx].flightPhase;

    // Update flight data
    g_FlightData[threadIdx].maxAltitude = max(g_FlightData[threadIdx].maxAltitude, h);
    g_FlightData[threadIdx].maxSurfSpeed = max(g_FlightData[threadIdx].maxSurfSpeed, length(g_TelemetryData[dataIdx].surfVelocity));
    g_FlightData[threadIdx].maxEciSpeed = max(g_FlightData[threadIdx].maxEciSpeed, length(g_TelemetryData[dataIdx].eciVelocity));
    g_FlightData[threadIdx].maxQ = max(g_FlightData[threadIdx].maxQ, Q);
    g_FlightData[threadIdx].minMass = min(g_FlightData[threadIdx].minMass, g_TelemetryData[dataIdx].mass);
    g_FlightData[threadIdx].maxAccel = max(g_FlightData[threadIdx].maxAccel, length(acceleration));
    g_FlightData[threadIdx].stage = g_TelemetryData[dataIdx].stage;
}
