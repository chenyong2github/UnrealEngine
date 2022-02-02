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
// 	FVector3f RelativeWorldPosition;
// 	FVector3f TilePosition;
	FVector3f TranslatedWorldPosition;
	FVector3f Normal;
	FVector3f dPdu;
	FVector3f dPdv;
	FVector3f Color;
	FVector3f Dimensions; // Radius,SoftRadius,Length or RectWidth,RectHeight or Sin(Angle/2),Sin(SoftAngle/2) depending on light type
	FVector2f Shaping;  // Barndoor controls for RectLights, Cone angles for spots lights
	float   Attenuation;
	float   FalloffExponent; // for non-inverse square decay lights only
	int32   IESTextureSlice;
	uint32  Flags; // see defines PATHTRACER_FLAG_*
	int32   RectLightTextureIndex;
	FVector3f TranslatedBoundMin;
	FVector3f TranslatedBoundMax;
	float Padding; // keep structure aligned
};

static_assert(sizeof(FPathTracingLight) == 128, "Path tracing light structure should be aligned to 128 bytes for optimal access on the GPU");

struct FPathTracingPackedPathState {
	uint32    PixelIndex;
	uint32    RandSeqSampleIndex;
	uint32    RandSeqSampleSeed;
	FVector3f Radiance;
	float     BackgroundVisibility;
	uint16    Albedo[3];
	uint16    Normal[3];
	FVector3f RayOrigin;
	FVector3f RayDirection;
	uint32    RayCone;
	FVector3f PathThroughput;
	uint16    PathRoughness;
	uint16    SigmaT[3];
};

static_assert(sizeof(FPathTracingPackedPathState) == 88, "Packed Path State size should be minimized");

#else

// HLSL side of the structs above

struct FPathTracingLight {
// 	float3  RelativeWorldPosition;
// 	float3  TilePosition;
	float3  TranslatedWorldPosition;
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
	float3  TranslatedBoundMin;
	float3  TranslatedBoundMax;
	float Padding;
};

struct FPathTracingPackedPathState {
	uint      PixelIndex;
	uint      RandSeqSampleIndex;
	uint      RandSeqSampleSeed;
	float3    Radiance;
	float     BackgroundVisibility;
	uint3     PackedAlbedoNormal;
	float3    RayOrigin;
	float3    RayDirection;
	uint      PackedRayCone;
	float3    PathThroughput;
	uint2     PackedRoughnessSigma;
};

#endif
