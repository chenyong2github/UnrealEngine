// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: public interface for hair strands rendering.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Containers/Array.h"
#include "Engine/EngineTypes.h"
#include "Shader.h"
#include "RenderResource.h"

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
	RenderVisCluster,
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

struct FHairStrandClusterCullingData;
struct FPooledRDGBuffer;
struct IPooledRenderTarget;
struct FRWBuffer;
class  FHairGroupPublicData;
class  FRDGShaderResourceView;
class  FResourceArrayInterface;
class  FSceneView;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Public group data 
class RENDERER_API FHairGroupPublicData : public FRenderResource
{
public:
	FHairGroupPublicData(uint32 GroupIndex, uint32 GroupInstanceVertexCount, uint32 ClusterCount, uint32 VertexCount);
	
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;
	virtual FString GetFriendlyName() const override { return TEXT("FHairGroupPublicData"); }

	// The primitive count when no culling and neither lod happens
	uint32 GetGroupInstanceVertexCount() const { return GroupInstanceVertexCount; }

	uint32 GetGroupIndex() const { return GroupIndex; }

	FRWBuffer& GetDrawIndirectBuffer()	{ return DrawIndirectBuffer; }
	FRWBuffer& GetClusterAABBBuffer()	{ return ClusterAABBBuffer; }
	FRWBuffer& GetGroupAABBBuffer()		{ return GroupAABBBuffer; }

	FRWBuffer& GetCulledVertexIdBuffer() { return CulledVertexIdBuffer; }
	FRWBuffer& GetCulledVertexRadiusScaleBuffer() { return CulledVertexRadiusScaleBuffer; }
	bool GetCullingResultAvailable() const { return bCullingResultAvailable; }
	void SetCullingResultAvailable(bool b) { bCullingResultAvailable = b; }

private:

	uint32 GroupInstanceVertexCount;
	uint32 GroupIndex;
	uint32 ClusterCount;
	uint32 VertexCount;

	/* Indirect draw buffer to draw everything or the result of the culling per pass */
	FRWBuffer DrawIndirectBuffer;

	/* Hair Cluster & Hair Group bounding box buffer */
	FRWBuffer ClusterAABBBuffer;
	FRWBuffer GroupAABBBuffer;

	/* Culling & LODing results for a hair group */ // Better to be transient?
	FRWBuffer CulledVertexIdBuffer;
	FRWBuffer CulledVertexRadiusScaleBuffer;
	bool bCullingResultAvailable = false;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster information exchanged between renderer and the hair strand plugin. 

struct FHairStrandClusterData
{
	struct FHairGroup
	{
		uint32 ClusterCount = 0;
		uint32 VertexCount = 0;

		float LodBias = 0.0f;
		float LodAverageVertexPerPixel = 0.0f;

		// See FHairStrandsClusterCullingResource fro details about those buffers.
		FRWBuffer* GroupAABBBuffer = nullptr;
		FRWBuffer* ClusterAABBBuffer = nullptr;
		FReadBuffer* ClusterInfoBuffer = nullptr;
		FReadBuffer* VertexToClusterIdBuffer = nullptr;
		FReadBuffer* ClusterVertexIdBuffer = nullptr;
		FReadBuffer* ClusterIndexRadiusScaleInfoBuffer = nullptr;

		// Culling & LOD output
		FRWBuffer* GetCulledVertexIdBuffer() const { return HairGroupPublicPtr ? &HairGroupPublicPtr->GetCulledVertexIdBuffer() : nullptr; }
		FRWBuffer* GetCulledVertexRadiusScaleBuffer() const { return HairGroupPublicPtr ? &HairGroupPublicPtr->GetCulledVertexRadiusScaleBuffer() : nullptr; }
		bool GetCullingResultAvailable() const { return HairGroupPublicPtr ? HairGroupPublicPtr->GetCullingResultAvailable() : false; }
		void SetCullingResultAvailable(bool b) { if (HairGroupPublicPtr) HairGroupPublicPtr->SetCullingResultAvailable(b); }

		TRefCountPtr<FPooledRDGBuffer> ClusterDebugAABBBuffer;							// Null if this debug is not enabled.
		TRefCountPtr<FPooledRDGBuffer> CulledDispatchIndirectParametersClusterCount;	// Null if this debug is not enabled.

		FHairGroupPublicData* HairGroupPublicPtr = nullptr;
	};

	TArray<FHairGroup> HairGroups;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Hair/Mesh projection & interpolation

typedef void(*THairStrandsResetInterpolationFunction)(
	FRHICommandListImmediate& RHICmdList,
	struct FHairStrandsInterpolationInput* Input,
	struct FHairStrandsInterpolationOutput* Output);

typedef void (*THairStrandsInterpolationFunction)(
	FRHICommandListImmediate& RHICmdList, 
	const struct FShaderDrawDebugData* ShaderDrawData,
	struct FHairStrandsInterpolationInput* Input, 
	struct FHairStrandsInterpolationOutput* Output, 
	struct FHairStrandsProjectionHairData& RenHairProjection,
	struct FHairStrandsProjectionHairData& SimHairProjection,
	int32 LODIndex,
	FHairStrandClusterData* ClusterData);

struct FHairStrandsInterpolationData
{
	struct FHairStrandsInterpolationInput* Input = nullptr;
	struct FHairStrandsInterpolationOutput* Output = nullptr;
	THairStrandsInterpolationFunction Function = nullptr;
	THairStrandsResetInterpolationFunction ResetFunction = nullptr; 
};

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

	struct LOD
	{
		TArray<Section> Sections;
	};
	TArray<LOD> LODs;
};

struct FHairStrandsProjectionHairData
{
	struct LODData
	{
		enum class EStatus { Invalid, Initialized, Completed };
		bool bIsValid = false;
		int32 LODIndex = -1;
		EStatus* Status = nullptr;

		// The index buffers stores the mesh section & the triangle index into a single uint32 
		// (3 highest bits store the section (up to 8 sections)
		FRWBuffer* RootTriangleIndexBuffer = nullptr;
		FRWBuffer* RootTriangleBarycentricBuffer = nullptr;

		// Rest root triangles' positions are relative to root center (for preserving precision)
		FVector* RestPositionOffset = nullptr;
		FRWBuffer* RestRootTrianglePosition0Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition1Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition2Buffer = nullptr;

		// Deformed root triangles' positions are relative to root center (for preserving precision)
		FVector* DeformedPositionOffset = nullptr;
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

////////////////////////////////////////////////////////////////////////////////////////////////////
// Hair component/primitive resources (shared with the engine side)
struct FHairStrandsPrimitiveResources
{
	struct FHairGroup
	{
		FRWBuffer* ClusterAABBBuffer = nullptr;
		FRWBuffer* GroupAABBBuffer = nullptr;
		uint32 ClusterCount = 0;
	};
	TArray<FHairGroup> Groups;
};

FHairStrandsPrimitiveResources GetHairStandsPrimitiveResources(uint64 Id);

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
	const FHairStrandsPrimitiveResources& PrimitiveResources,
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

RENDERER_API bool IsHairRayTracingEnabled();

RENDERER_API void RunProjection(
	FRHICommandListImmediate& RHICmdList,
	const FTransform& LocalToWorld,
	const FVector& RestPositionOffset,
	const FHairStrandsProjectionMeshData& MeshData,
	FHairStrandsProjectionHairData& RenProjectionHairData,
	FHairStrandsProjectionHairData& SimProjectionHairData);