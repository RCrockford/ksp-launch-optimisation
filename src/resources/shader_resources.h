// Defines for shader resource in the global descriptor table

#ifndef SHADER_RESOURCES_H
#define SHADER_RESOURCES_H

#ifdef __cplusplus
namespace ShaderShared
{

class CBV_TYPE
{
    const unsigned value;
    const unsigned count;
public:
    explicit CBV_TYPE(const unsigned v) : value(v), count(1) {}
    explicit CBV_TYPE(const unsigned v, const unsigned c) : value(v), count(c) {}

    friend unsigned getCbvOffset(const CBV_TYPE srv, unsigned index = 0);
    friend unsigned getCbvCount();
    friend unsigned getDescriptorCount();
};

class SRV_TYPE
{
    const unsigned value;
    const unsigned count;
public:
    explicit SRV_TYPE(const unsigned v) : value(v), count(1) {}
    explicit SRV_TYPE(const unsigned v, const unsigned c) : value(v), count(c) {}

    friend unsigned getSrvOffset(const SRV_TYPE srv, unsigned index = 0);
    friend unsigned getSrvCount();
    friend unsigned getDescriptorCount();
};

class UAV_TYPE
{
    const unsigned value;
    const unsigned count;
public:
    explicit UAV_TYPE(const unsigned v) : value(v), count(1) {}
    explicit UAV_TYPE(const unsigned v, const unsigned c) : value(v), count(c) {}

    friend unsigned getUavOffset(const UAV_TYPE srv, unsigned index = 0);
    friend unsigned getUavCount();
    friend unsigned getDescriptorCount();
};

#define CBV_VALUE   CBV_TYPE
#define SRV_VALUE   SRV_TYPE
#define UAV_VALUE   UAV_TYPE

#else

#define CBV_TYPE        int
#define SRV_TYPE        int
#define UAV_TYPE        int

#define CBV_VALUE(v,c)  int(v)
#define SRV_VALUE(v,c)  int(v)
#define UAV_VALUE(v,c)  int(v)

#endif

//----------

static const CBV_TYPE cbvMissionParams = CBV_VALUE(0,1);
static const CBV_TYPE cbvEnviroParams = CBV_VALUE(1,1);

static const CBV_TYPE cbvCount = CBV_VALUE(2,1); // must be last CBV

//----------

static const SRV_TYPE srvFontTexture = SRV_VALUE(0,1);
static const SRV_TYPE srvTelemetryData = SRV_VALUE(1,1);
static const SRV_TYPE srvFlightData = SRV_VALUE(2,1);
static const SRV_TYPE srvPressureHeightCurve = SRV_VALUE(3,1);
static const SRV_TYPE srvTemperatureHeightCurve = SRV_VALUE(4,1);
static const SRV_TYPE srvAscentParams = SRV_VALUE(5,1);
static const SRV_TYPE srvGraphColours = SRV_VALUE(6,1);
static const SRV_TYPE srvLiftMachCurve = SRV_VALUE(7,4);
static const SRV_TYPE srvDragMachCurve = SRV_VALUE(11,4);

static const SRV_TYPE srvCount = SRV_VALUE(15,1); // must be last SRV

//----------

static const UAV_TYPE uavTelemetryData = UAV_VALUE(0,1);
static const UAV_TYPE uavFlightData = UAV_VALUE(1,1);
static const UAV_TYPE uavDebugTelemetryData = UAV_VALUE(2,1);

static const UAV_TYPE uavCount = UAV_VALUE(3,1); // must be last UAV

//----------

#ifdef __cplusplus

inline unsigned getCbvOffset( const CBV_TYPE cbv, unsigned index )
{
    if (index < cbv.count)
        return cbv.value + index;
    return cbv.value;
}

inline unsigned getCbvCount()
{
    return cbvCount.value;
}

//----------

inline unsigned getSrvOffset( const SRV_TYPE srv, unsigned index )
{
    if ( index < srv.count )
        return getCbvOffset( cbvCount ) + srv.value + index;
    return getCbvOffset( cbvCount ) + srv.value;
}

inline unsigned getSrvCount()
{
    return srvCount.value;
}

//----------

inline unsigned getUavOffset( const UAV_TYPE uav, unsigned index )
{
    if ( index < uav.count )
        return getSrvOffset( srvCount ) + uav.value + index;
    return getSrvOffset( srvCount ) + uav.value;
}

inline unsigned getUavCount()
{
    return uavCount.value;
}

//----------

inline unsigned getDescriptorCount()
{
    return cbvCount.value + srvCount.value + uavCount.value;
}

}

#else

SamplerState g_samplerBilinear : register(s0);
SamplerState g_samplerPoint : register(s1);

#endif

#endif
