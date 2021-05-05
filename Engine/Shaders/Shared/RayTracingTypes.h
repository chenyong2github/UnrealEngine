// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/*================================================================================================
	RayTracingTypes.h: used in ray tracing shaders and C++ code to define common types
	!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#ifdef __cplusplus

// C++ representation of a light for the path tracer
// #dxr_todo: Unify this with FRTLightingData ?
struct FPathTracingLight {
	FVector3f Position;
	FVector3f Normal;
	FVector3f dPdu;
	FVector3f dPdv;
	FVector3f Color;
	FVector3f Dimensions; // Radius,SoftRadius,Length or RectWidth,RectHeight or Sin(Angle/2),Sin(SoftAngle/2) depending on light type
	FVector2D Shaping;  // Barndoor controls for RectLights, Cone angles for spots lights
	float   Attenuation;
	float   FalloffExponent; // for non-inverse square decay lights only
	int32   IESTextureSlice;
	uint32  Flags; // see defines PATHTRACER_FLAG_*
	int32   RectLightTextureIndex;
	FVector3f BoundMin;
	FVector3f BoundMax;
	float Padding; // keep structure aligned
};

static_assert(sizeof(FPathTracingLight) == 128, "Path tracing light structure should be aligned to 128 bytes for optimal access on the GPU");

#else

// HLSL side of the struct above

struct FPathTracingLight {
	float3  Position;
	float3  Normal;
	float3  dPdu;
	float3  dPdv;
	float3  Color;
	float3  Dimensions;
	float2  Shaping;
	float   Attenuation;
	float   FalloffExponent;
	int     IESTextureSlice;
	uint    Flags;
	int	    RectLightTextureIndex;
	float3  BoundMin;
	float3  BoundMax;
	float Padding;
};

#endif
