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
#include "RenderGraphResources.h"

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

struct FMinHairRadiusAtDepth1
{
	float Primary = 1;
	float Velocity = 1;
	float Stable = 1;
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

	struct VertexFactoryInput 
	{
		FShaderResourceViewRHIRef HairPositionBuffer = nullptr;
		FVector HairPositionOffset = FVector::ZeroVector;
		uint32 VertexCount = 0;
		float HairRadius = 0;
		float HairLength = 0;
		float HairDensity = 0;
		bool bUseStableRasterization = false;
		bool bScatterSceneLighting = false;
		FTransform LocalToWorldTransform;
	};
	VertexFactoryInput VFInput;

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
	struct FHairStrandsInterpolationOutput* Output,
	struct FHairStrandsProjectionHairData& SimHairProjection,
	int32 LODIndex);

typedef void (*THairStrandsInterpolationFunction)(
	FRHICommandListImmediate& RHICmdList, 
	const struct FShaderDrawDebugData* ShaderDrawData,
	const FTransform& LocalToWorld,
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
		FRHIShaderResourceView* UVsBuffer = nullptr;
		FRHIShaderResourceView* IndexBuffer = nullptr;
		uint32 UVsChannelCount = 0;
		uint32 UVsChannelOffset = 0;
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
	enum class EStatus { Invalid, Initialized, Completed };

	struct RestLODData
	{
		int32 LODIndex = -1;
		EStatus* Status = nullptr;
		inline bool IsValid() const { return Status && (*Status == EStatus::Initialized || *Status == EStatus::Completed); }

		// The index buffers stores the mesh section & the triangle index into a single uint32 
		// (3 highest bits store the section (up to 8 sections)
		//
		// See EncodeTriangleIndex & DecodeTriangleIndex functions in HairStrandsMeshProjectionCommon.ush
		FRWBuffer* RootTriangleIndexBuffer = nullptr;
		FRWBuffer* RootTriangleBarycentricBuffer = nullptr;

		// Rest root triangles' positions are relative to root center (for preserving precision)
		FRWBuffer* RestRootTrianglePosition0Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition1Buffer = nullptr;
		FRWBuffer* RestRootTrianglePosition2Buffer = nullptr;

		// Samples to be used for RBF mesh interpolation
		uint32 SampleCount = 0;
		FRWBuffer* MeshInterpolationWeightsBuffer = nullptr;
		FRWBuffer* MeshSampleIndicesBuffer = nullptr;
		FRWBuffer* RestSamplePositionsBuffer = nullptr;
	};

	struct DeformedLODData
	{
		int32 LODIndex = -1;
		EStatus* Status = nullptr;
		inline bool IsValid() const { return Status && (*Status == EStatus::Initialized || *Status == EStatus::Completed); }

		// Deformed root triangles' positions are relative to root center (for preserving precision)
		FRWBuffer* DeformedRootTrianglePosition0Buffer = nullptr;
		FRWBuffer* DeformedRootTrianglePosition1Buffer = nullptr;
		FRWBuffer* DeformedRootTrianglePosition2Buffer = nullptr;

		// Samples to be used for RBF mesh interpolation
		FRWBuffer* DeformedSamplePositionsBuffer = nullptr;
		FRWBuffer* MeshSampleWeightsBuffer = nullptr;
	};

	struct HairGroup
	{
		FRHIShaderResourceView* RootPositionBuffer = nullptr;
		FRHIShaderResourceView* RootNormalBuffer = nullptr;
		FRWBuffer* VertexToCurveIndexBuffer = nullptr;

		TArray<RestLODData> RestLODDatas;
		TArray<DeformedLODData> DeformedLODDatas;

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

FHairStrandsPrimitiveResources GetHairStandsPrimitiveResources(uint32 ComponentId);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug infos

struct FHairStrandsDebugInfo
{
	uint32 ComponentId = 0;
	EWorldType::Type WorldType = EWorldType::None;
	FString GroomAssetName;
	FString SkeletalComponentName;

	struct HairGroup
	{
		float MaxRadius = 0;
		float MaxLength = 0;
		uint32 VertexCount = 0;
		uint32 CurveCount = 0;

		bool bHasSkinInterpolation = false;
		bool bHasBinding = false;
		bool bHasSimulation = false;
		uint32 LODCount = 0;
	};
	TArray<HairGroup> HairGroups;
};

typedef TArray<FHairStrandsDebugInfo> FHairStrandsDebugInfos;
FHairStrandsDebugInfos GetHairStandsDebugInfos();


////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesh transfer and hair projection debug infos

enum class EHairStrandsProjectionMeshType
{
	RestMesh,
	DeformedMesh,
	SourceMesh,
	TargetMesh
};

struct FHairStrandsProjectionDebugInfo
{
	FHairStrandsProjectionMeshData SourceMeshData;
	FHairStrandsProjectionMeshData TargetMeshData;
	TArray<FRWBuffer> TransferredPositions;
	FString GroomAssetName;
	FString SkeletalComponentName;

};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Registrations

RENDERER_API void RegisterHairStrands(
	uint32 ComponentId,
	uint32 SkeletalComponentId,
	EWorldType::Type WorldType,
	const FHairStrandsInterpolationData& E,
	const FHairStrandsProjectionHairData& RenProjection,
	const FHairStrandsProjectionHairData& SimProjection,
	const FHairStrandsPrimitiveResources& PrimitiveResources,
	const FHairStrandsDebugInfo& DebugInfo,
	const FHairStrandsProjectionDebugInfo& DebugProjectionInfo);

RENDERER_API void UnregisterHairStrands(uint32 ComponentId);

RENDERER_API bool UpdateHairStrands(
	uint32 ComponentId,
	EWorldType::Type NewWorldType);

RENDERER_API bool UpdateHairStrands(
	uint32 ComponentId,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FHairStrandsProjectionHairData& RenProjection,
	const FHairStrandsProjectionHairData& SimProjection);

RENDERER_API bool UpdateHairStrands(
	uint32 ComponentId,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FTransform& MeshLocalToWorld);

RENDERER_API bool UpdateHairStrandsDebugInfo(
	uint32 ComponentId,
	EWorldType::Type WorldType,
	const uint32 GroupIt,
	const bool bSimulationEnable);

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

RENDERER_API void RunMeshTransfer(
	FRHICommandListImmediate& RHICmdList,
	const FHairStrandsProjectionMeshData& SourceMeshData,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	TArray<FRWBuffer>& TransferredLODsPositions);

RENDERER_API void RunProjection(
	FRHICommandListImmediate& RHICmdList,
	const FTransform& LocalToWorld,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	FHairStrandsProjectionHairData& RenProjectionHairData,
	FHairStrandsProjectionHairData& SimProjectionHairData);

RENDERER_API FHairStrandsProjectionMeshData ExtractMeshData(class FSkeletalMeshRenderData* RenderData);
FHairStrandsProjectionHairData::HairGroup ToProjectionHairData(struct FHairStrandsRestRootResource* InRest, struct FHairStrandsDeformedRootResource* InDeformed);

typedef void (*TBindingProcess)(FRHICommandListImmediate& RHICmdList, void* Asset);
RENDERER_API void EnqueueGroomBindingQuery(void* Asset, TBindingProcess BindingProcess);


struct FFollicleInfo
{
	enum EChannel {R = 0, G = 1, B = 2, A = 3};

	uint32 GroomId = ~0;
	EChannel Channel = R;
	uint32 KernelSizeInPixels = 0;
};

RENDERER_API void EnqueueFollicleMaskUpdateQuery(const TArray<FFollicleInfo>& Infos, class UTexture2D* OutTexture);

void RunHairStrandsProcess(FRHICommandListImmediate& RHICmdList, class FGlobalShaderMap* ShaderMap); 
bool HasHairStrandsProcess(EShaderPlatform Platform);


typedef TArray<FRHIUnorderedAccessView*> FBufferTransitionQueue;
RENDERER_API void TransitBufferToReadable(FRHICommandListImmediate& RHICmdList, FBufferTransitionQueue& BuffersToTransit);
