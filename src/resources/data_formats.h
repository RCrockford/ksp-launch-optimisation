// Defines for shader resource in the global descriptor table

#ifndef DATA_FORMATS_H
#define DATA_FORMATS_H

#ifdef __cplusplus
namespace ShaderShared
{
    typedef int int1;

    struct int2
    {
        int     x, y;
        int2() {}
        int2(int x_, int y_) : x(x_), y(y_) {}
    };

    struct int3
    {
        int     x, y, z;
        int3() {}
        int3(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    };

    __declspec(align(16)) struct int4
    {
        int     x, y, z, w;
        int4() {}
        int4(int x_, int y_, int z_, int w_) : x(x_), y(y_), z(z_), w(w_) {}
    };

    typedef unsigned      uint;
    typedef unsigned      uint1;

    struct uint2
    {
        uint     x, y;
        uint2() {}
        uint2(uint x_, uint y_) : x(x_), y(y_) {}
    };

    struct uint3
    {
        uint     x, y, z;
        uint3() {}
        uint3(uint x_, uint y_, uint z_) : x(x_), y(y_), z(z_) {}
    };

    __declspec(align(16)) struct uint4
    {
        uint     x, y, z, w;
        uint4() {}
        uint4(uint x_, uint y_, uint z_, uint w_) : x(x_), y(y_), z(z_), w(w_) {}
    };

    typedef float     float1;

    struct float2
    {
        float     x, y;
        float2() {}
        float2(float x_, float y_) : x(x_), y(y_) {}
    };

    struct float3
    {
        float     x, y, z;
        float3() {}
        float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    };

    inline float3 operator+(const float3& lhs, const float3& rhs)
    {
        float3 r;
        r.x = lhs.x + rhs.x;
        r.y = lhs.y + rhs.y;
        r.z = lhs.z + rhs.z;
        return r;
    }

    inline float3 operator-(const float3& lhs, const float3& rhs)
    {
        float3 r;
        r.x = lhs.x - rhs.x;
        r.y = lhs.y - rhs.y;
        r.z = lhs.z - rhs.z;
        return r;
    }

    __declspec(align(16)) struct float4
    {
        float     x, y, z, w;
        float4() {}
        float4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    };
#endif

// Stored each sample
struct TelemetryData
{
    float2  eciPosition;    // m relative to earth centre
    float2  eciVelocity;    // m/s
    float2  surfVelocity;   // m/s
    float2  heading;        // unit vector
    uint    stage;
    float   mass;           // kg
    uint    flightPhase;
    float   guidancePitch;  // radians
    float4  T;
};

// Flight phases
static const uint   c_PhaseLiftoff          = 0;
static const uint   c_PhasePitchOver        = 1;
static const uint   c_PhaseAeroFlight       = 2;
static const uint   c_PhaseGuidanceReady    = 3;
static const uint   c_PhaseGuidanceActive   = 4;
static const uint   c_PhaseMECO             = 5;

struct GuidanceData
{
    float   A;      // steering constant
    float   B;      // steering constant (/sec)
    float   T;      // burn time estimate
    float   t;      // time since last guidance update
    float   omegaT; // Angular speed at T
};

// Stored for each ascent profile
struct FlightData
{
    float   maxAltitude;
    float   maxSurfSpeed;
    float   maxEciSpeed;
    float   maxQ;
    float   minMass;
    float   maxAccel;
    uint    flightPhase;
    uint    stage;
    float   stageBurnTime[4];

    // Orbital params
    float   a;
    float   e;
    float   E;
    
    // Guidance data, per stage
    GuidanceData    guidance[4];
};

// Stored for each ascent profile
struct AscentParams
{
    float   pitchOverSpeed;     // m/s
    float   sinPitchOverAngle;
    float   cosPitchOverAngle;
    float   sinAimAngle;     // 1 degree more than pitch over angle
    float   cosAimAngle;
};

struct StageData
{
    float   wetMass;        // kg
    float   dryMass;        // kg
    float   massFlow;       // kg/s
    float   IspSL;          // s
    float   IspVac;         // s
    float   rotationRate;   // radian/s
    float2  pad;
};

// A rotating inertial reference frame.
struct ReferenceFrame
{
    float r;        // radial distance
    float rv;       // radial velocity
    float h;        // angular momentum
    float omega;    // angular speed
};

// Shared across all profiles
struct MissionParams
{
    StageData stage[4];
    uint    stageCount;

    uint2   pad;
    float   finalOrbitalEnergy;
    ReferenceFrame  finalState;
};

// Given Ap, Pe (as altitudes)
// h = sqrt(mu * L)
// a = (Ap + Pe) / 2 + Re
// E = -mu / (2a)
// e = 1 - (Pe + Re) / a
// L = a(1 - e^2)

struct FrameConstData
{
    float   timeStep;
    uint    simDataStep;        // Step size to get to next sample.
    uint    selectedData;
    uint    selectionFlash;
};

// Ideally draw consts should be <=12 floats in size
struct DrawConstData
{
    uint    graphDataOffset;    // Data offset at vertex 0 (buffer index rather than sample).
    float   graphDiscontinuity; // Sample index of discontinuity.
    float   graphViewportScale; // Scale factor for vertex index to viewport.
    float   graphSampleXScale;  // Scale factor for vertex index to sample.
    float2  viewportSize;       // In pixels
    float2  invViewportSize;
};

// Ideally dispatch consts should be <=12 floats in size
struct DispatchConstData
{
    uint    srcDataOffset;      // Buffer index for first source datum of this sample.
    uint    dstDataOffset;      // Buffer index for first dest datum of this sample.
};

#ifndef __cplusplus
ConstantBuffer<FrameConstData>  c_frameConsts   : register(b1, space1);

#ifdef SHADER_TYPE_CS
ConstantBuffer<DispatchConstData>   c_dispatchConsts    : register(b0, space1);
#else
ConstantBuffer<DrawConstData>       c_drawConsts        : register(b0, space1);
#endif

static const float  c_Pi = 3.14159265;
static const float  c_RadToDegree = 180.0 / c_Pi;
static const float  c_DegreeToRad = c_Pi / 180.0;

static const uint   c_ThreadWidth = 16;
static const uint   c_ThreadHeight = 32;

static const uint   c_FlightDataMin = (c_ThreadWidth * c_ThreadHeight);
static const uint   c_FlightDataMax = (c_ThreadWidth * c_ThreadHeight + 1);

float sqr(float x)
{
    return x * x;
}
float cube(float x)
{
    return x * x * x;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
