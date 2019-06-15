// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// In order to support resonance cross platform, we need to be able to compile Resonance on platforms we may not support Embree on.
// For those, we noop on all RTC calls:

#if !USE_EMBREE

#include "ResonanceEmbreeHelper.h"
#include "CoreMinimal.h"

void rtcCommit(RTCScene scene)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

void rtcDeleteScene(RTCScene scene)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

void rtcDeleteDevice(RTCDevice device)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

RTCScene rtcDeviceNewScene(RTCDevice device, RTCSceneFlags flags, RTCAlgorithmFlags aflags)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
	return nullptr;
}

void rtcIntersect(RTCScene scene, RTCRay& ray)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

void* rtcMapBuffer(RTCScene scene, unsigned geomID, RTCBufferType type)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
	return nullptr;
}

void rtcUnmapBuffer(RTCScene scene, unsigned geomID, RTCBufferType type)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

RTCDevice rtcNewDevice(const char* cfg)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
	return nullptr;
}

unsigned rtcNewTriangleMesh(RTCScene scene,                    //!< the scene the mesh belongs to
	RTCGeometryFlags flags,            //!< geometry flags
	size_t numTriangles,               //!< number of triangles
	size_t numVertices,                //!< number of vertices
	size_t numTimeSteps               //!< number of motion blur time steps
)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
	return INDEX_NONE;
}

unsigned rtcNewUserGeometry(RTCScene scene,           /*!< the scene the user geometry set is created in */
	size_t numGeometries      /*!< the number of geometries contained in the set */)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
	return INDEX_NONE;
}

void rtcSetBoundsFunction(RTCScene scene, unsigned geomID, RTCBoundsFunc bounds)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

void rtcSetIntersectFunction(RTCScene scene, unsigned geomID, RTCIntersectFunc intersect)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

void rtcSetUserData(RTCScene scene, unsigned geomID, void* ptr)
{
	checkf(false, TEXT("Embree not compiled for this platform! To support raytracing in Resonance, compile Embree for this platform and add it in ResonanceAudio.build.cs."));
}

#endif // USE_EMBREE