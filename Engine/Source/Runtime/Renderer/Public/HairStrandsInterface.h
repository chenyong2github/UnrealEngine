// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: public interface for hair strands rendering.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Containers/Array.h"
#include "Engine/EngineTypes.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc/Helpers

enum class EHairStrandsDebugMode : uint8
{
	None,
	SimHairStrands,
	RenderHairStrands,
	RenderHairUV,
	RenderHairRootUV,
	RenderHairRootUDIM,
	RenderHairSeed,
	RenderHairDimension,
	RenderHairRadiusVariation,
	RenderHairBaseColor,
	RenderHairRoughness,
	Count
};

/// Return the active debug view mode
RENDERER_API EHairStrandsDebugMode GetHairStrandsDebugStrandsMode();

/// Return the number of sample subsample count used for the visibility pass
RENDERER_API uint32 GetHairVisibilitySampleCount();

struct FMinHairRadiusAtDepth1
{
	float Primary = 1;
	float Velocity = 1;
};

/// Compute the strand radius at a distance of 1 meter
RENDERER_API FMinHairRadiusAtDepth1 ComputeMinStrandRadiusAtDepth1(
	const FIntPoint& Resolution,
	const float FOV,
	const uint32 SampleCount,
	const float OverrideStrandHairRasterizationScale);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Hair/Mesh projection & interpolation
typedef void (*THairStrandsInterpolationFunction)(
	FRHICommandListImmediate& RHICmdList, 
	struct FHairStrandsInterpolationInput* Input, 
	struct FHairStrandsInterpolationOutput* Output, 
	struct FHairStrandsProjectionHairData& RenHairProjection,
	struct FHairStrandsProjectionHairData& SimHairProjection,
	int32 LODIndex);

struct FHairStrandsInterpolationData
{
	struct FHairStrandsInterpolationInput* Input = nullptr;
	struct FHairStrandsInterpolationOutput* Output = nullptr;
	THairStrandsInterpolationFunction Function = nullptr;
};

struct FRWBuffer;

struct FHairStrandsProjectionMeshData
{
	struct Section
	{
		FTransform LocalToWorld;
		FRHIShaderResourceView* PositionBuffer = nullptr;
		FRHIShaderResourceView* IndexBuffer = nullptr;
		uint32 NumPrimitives = 0;
		uint32 VertexBaseIndex = 0;
		uint32 IndexBaseIndex = 0;
		uint32 TotalVertexCount = 0;
		uint32 TotalIndexCount = 0;
		uint32 SectionIndex = 0;
		int32 LODIndex = 0;
	};

	TArray<Section> Sections;
};

struct FHairStrandsProjectionHairData
{
	struct LODData
	{
		bool bIsValid = false;
		int32 LODIndex = -1;

		// The index buffers stores the mesh section & the triangle index into a single uint32 
		// (3 highest bits store the section (up to 8 sections)
		FRWBuffer* RootTriangleIndexBuffer = nullptr;
		FRWBuffer* RootTriangleBarycentricBuffer = nullptr;

		// Rest root triangles' positions are relative to root center (for preserving precision)
		FVector RestPositionOffset = FVector::ZeroVector;
		FRWBuffer* RestRootTrianglePosition0Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition1Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition2Buffer = nullptr;

		// Deformed root triangles' positions are relative to root center (for preserving precision)
		FVector DeformedPositionOffset = FVector::ZeroVector;
		FRWBuffer* DeformedRootTrianglePosition0Buffer = nullptr;
		FRWBuffer* DeformedRootTrianglePosition1Buffer = nullptr;
		FRWBuffer* DeformedRootTrianglePosition2Buffer = nullptr;
	};

	struct HairGroup
	{
		FRHIShaderResourceView* RootPositionBuffer = nullptr;
		FRHIShaderResourceView* RootNormalBuffer = nullptr;
		FRWBuffer* VertexToCurveIndexBuffer = nullptr;

		TArray<LODData> LODDatas;

		uint32 RootCount = 0;
		FTransform LocalToWorld = FTransform::Identity;
	};

	TArray<HairGroup> HairGroups;
};

RENDERER_API void AddHairStrandsProjectionQuery(FRHICommandListImmediate& RHICmdList, uint64 Id, EWorldType::Type WorldType, int32 LODIndex, const FVector& RestRootCenter);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug infos

struct FHairStrandsDebugInfo
{
	uint64 Id = 0;
	EWorldType::Type WorldType = EWorldType::None;

	struct HairGroup
	{
		float MaxRadius = 0;
		float MaxLength = 0;
		uint32 VertexCount = 0;
		uint32 CurveCount = 0;

		bool bHasSkinInterpolation = false;
		uint32 LODCount = 0;
	};
	TArray<HairGroup> HairGroups;
};

typedef TArray<FHairStrandsDebugInfo> FHairStrandsDebugInfos;
FHairStrandsDebugInfos GetHairStandsDebugInfos();

////////////////////////////////////////////////////////////////////////////////////////////////////
// Registrations

RENDERER_API void RegisterHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FHairStrandsInterpolationData& E,
	const FHairStrandsProjectionHairData& RenProjection,
	const FHairStrandsProjectionHairData& SimProjection,
	const FHairStrandsDebugInfo& DebugInfo);

RENDERER_API void UnregisterHairStrands(uint64 Id);

RENDERER_API bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type NewWorldType);

RENDERER_API bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FHairStrandsProjectionHairData& RenProjection,
	const FHairStrandsProjectionHairData& SimProjection);

RENDERER_API bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FTransform& MeshLocalToWorld,
	const FVector& SkeletalDeformedPositionOffset);

RENDERER_API bool UpdateHairStrands(
	uint64 Id, 
	EWorldType::Type WorldType, 
	const class FSkeletalMeshObject* MeshObject);

RENDERER_API bool IsHairStrandsSupported(const EShaderPlatform Platform);
bool IsHairStrandsEnable(EShaderPlatform Platform);

// Return strands & guide indices to be preserved, while all others strands/guides should be culled
enum class EHairCullMode : uint8
{
	None,
	Render,
	Sim
};
struct FHairCullInfo
{
	int32 ExplicitIndex = -1; 
	float NormalizedIndex = 0; // [0,1]
	EHairCullMode CullMode = EHairCullMode::None;
};
RENDERER_API FHairCullInfo GetHairStrandsCullInfo();
