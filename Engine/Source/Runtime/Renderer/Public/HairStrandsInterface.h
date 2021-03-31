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
#include "GpuDebugRendering.h"

class UTexture2D;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Utils buffers for importing/exporting hair resources

enum class ERDGImportedBufferFlags
{
	None = 0,
	CreateSRV = 0x1,
	CreateUAV = 0x2,
	CreateViews = CreateSRV | CreateUAV
};
ENUM_CLASS_FLAGS(ERDGImportedBufferFlags);

struct RENDERER_API FRDGExternalBuffer
{
	TRefCountPtr<FRDGPooledBuffer> Buffer;
	FShaderResourceViewRHIRef SRV;
	FUnorderedAccessViewRHIRef UAV;
	EPixelFormat Format = PF_Unknown;
	void Release();
};

struct RENDERER_API FRDGImportedBuffer
{
	FRDGBufferRef Buffer = nullptr;
	FRDGBufferSRVRef SRV = nullptr;
	FRDGBufferUAVRef UAV = nullptr;
};

RENDERER_API FRDGImportedBuffer Register(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In, ERDGImportedBufferFlags Flags, ERDGUnorderedAccessViewFlags UAVFlags = ERDGUnorderedAccessViewFlags::None);
RENDERER_API FRDGBufferSRVRef   RegisterAsSRV(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In);
RENDERER_API FRDGBufferUAVRef   RegisterAsUAV(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In, ERDGUnorderedAccessViewFlags Flags = ERDGUnorderedAccessViewFlags::None);
RENDERER_API void				ConvertToExternalBufferWithViews(FRDGBuilder& GraphBuilder, FRDGBufferRef& InBuffer, FRDGExternalBuffer& OutBuffer, EPixelFormat Format = PF_Unknown);
////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc/Helpers

enum class EHairStrandsDebugMode : uint8
{
	NoneDebug,
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

enum class EHairDebugMode : uint8
{
	None,
	MacroGroups,
	LightBounds,
	DeepOpacityMaps,
	MacroGroupScreenRect,
	SamplePerPixel,
	CoverageType,
	TAAResolveType,
	VoxelsDensity,
	VoxelsTangent,
	VoxelsBaseColor,
	VoxelsRoughness,
	MeshProjection,
	Coverage,
	MaterialDepth,
	MaterialBaseColor,
	MaterialRoughness,
	MaterialSpecular,
	MaterialTangent,
	Tile
};

/// Return the active debug view mode
RENDERER_API EHairStrandsDebugMode GetHairStrandsDebugStrandsMode();
RENDERER_API EHairDebugMode GetHairStrandsDebugMode();

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
struct IPooledRenderTarget;
struct FRWBuffer;
class  FRDGPooledBuffer;
class  FHairGroupPublicData;
class  FRDGShaderResourceView;
class  FResourceArrayInterface;
class  FSceneView;

enum EHairGeometryType
{
	Strands,
	Cards,
	Meshes,
	NoneGeometry
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Public group data 
class RENDERER_API FHairGroupPublicData : public FRenderResource
{
public:
	FHairGroupPublicData(uint32 InGroupIndex);
	void SetClusters(uint32 InClusterCount, uint32 InVertexCount);
	
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;
	virtual FString GetFriendlyName() const override { return TEXT("FHairGroupPublicData"); }

	// The primitive count when no culling and neither lod happens
	uint32 GetGroupInstanceVertexCount() const { return GroupControlTriangleStripVertexCount; }
	uint32 GetGroupControlPointCount() const { return VertexCount; }

	uint32 GetGroupIndex() const { return GroupIndex; }

	FRDGExternalBuffer& GetDrawIndirectRasterComputeBuffer() { return DrawIndirectRasterComputeBuffer; }
	const FRDGExternalBuffer& GetDrawIndirectRasterComputeBuffer() const { return DrawIndirectRasterComputeBuffer; }
	FRDGExternalBuffer& GetDrawIndirectBuffer() { return DrawIndirectBuffer; }
	FRDGExternalBuffer& GetClusterAABBBuffer() { return ClusterAABBBuffer; }
	FRDGExternalBuffer& GetGroupAABBBuffer() { return GroupAABBBuffer; }

	const FRDGExternalBuffer& GetCulledVertexIdBuffer() const { return CulledVertexIdBuffer; }
	const FRDGExternalBuffer& GetCulledVertexRadiusScaleBuffer() const { return CulledVertexRadiusScaleBuffer; }

	FRDGExternalBuffer& GetCulledVertexIdBuffer() { return CulledVertexIdBuffer; }
	FRDGExternalBuffer& GetCulledVertexRadiusScaleBuffer() { return CulledVertexRadiusScaleBuffer; }

	bool GetCullingResultAvailable() const { return bCullingResultAvailable; }
	void SetCullingResultAvailable(bool b) { bCullingResultAvailable = b; }

	void SupportVoxelization(bool InVoxelize) { bSupportVoxelization = InVoxelize; }
	bool DoesSupportVoxelization() const { return bSupportVoxelization; }

	void SetLODGeometryTypes(const TArray<EHairGeometryType>& InTypes) { LODGeometryTypes = InTypes; }
	const TArray<EHairGeometryType>& GetLODGeometryTypes() const { return LODGeometryTypes; }

	void SetLODVisibilities(const TArray<bool>& InLODVisibility) { LODVisibilities = InLODVisibility; }
	const TArray<bool>& GetLODVisibilities() const { return LODVisibilities; }

	void SetLODScreenSizes(const TArray<float>& ScreenSizes) { LODScreenSizes = ScreenSizes; }
	const TArray<float>& GetLODScreenSizes() const { return LODScreenSizes;  }

	void SetLODBias(float InLODBias) { LODBias = InLODBias; }
	float GetLODBias() const { return LODBias; }

	void SetLODIndex(float InLODIndex) { LODIndex = InLODIndex; }
	float GetLODIndex() const { return LODIndex; }
	int32 GetIntLODIndex() const { return FMath::Max(0, FMath::FloorToInt(LODIndex)); }

	void SetLODVisibility(bool bVisible) { bLODVisibility = bVisible; }
	bool GetLODVisibility() const { return bLODVisibility; }

	uint32 GetClusterCount() const { return ClusterCount;  }
	struct FVertexFactoryInput 
	{
		struct FStrands
		{
			FShaderResourceViewRHIRef PositionBuffer = nullptr;
			FShaderResourceViewRHIRef PrevPositionBuffer = nullptr;
			FShaderResourceViewRHIRef TangentBuffer = nullptr;
			FShaderResourceViewRHIRef MaterialBuffer = nullptr;
			FShaderResourceViewRHIRef AttributeBuffer = nullptr;

			FVector PositionOffset = FVector::ZeroVector;
			FVector PrevPositionOffset = FVector::ZeroVector;

			FShaderResourceViewRHIRef PositionOffsetBuffer = nullptr;
			FShaderResourceViewRHIRef PrevPositionOffsetBuffer = nullptr;

			uint32 VertexCount = 0;
			float HairRadius = 0;
			float HairLength = 0;
			float HairDensity = 0;
			bool bUseStableRasterization = false;
			bool bScatterSceneLighting = false;
		} Strands;

		struct FCards
		{

		} Cards;

		struct FMeshes
		{
			
		} Meshes;

		bool bHasLODSwitch = false;
		EHairGeometryType GeometryType = EHairGeometryType::NoneGeometry;
		FTransform LocalToWorldTransform;
	};
	FVertexFactoryInput VFInput;
	uint32 ClusterDataIndex = ~0; // #hair_todo: move this into instance data, or remove FHairStrandClusterData
//private:

	uint32 GroupControlTriangleStripVertexCount;
	uint32 GroupIndex;
	uint32 ClusterCount;
	uint32 VertexCount;

	/* Indirect draw buffer to draw everything or the result of the culling per pass */
	FRDGExternalBuffer DrawIndirectBuffer;
	FRDGExternalBuffer DrawIndirectRasterComputeBuffer;

	/* Hair Cluster & Hair Group bounding box buffer */
	FRDGExternalBuffer ClusterAABBBuffer;
	FRDGExternalBuffer GroupAABBBuffer;

	/* Culling & LODing results for a hair group */ // Better to be transient?
	FRDGExternalBuffer CulledVertexIdBuffer;
	FRDGExternalBuffer CulledVertexRadiusScaleBuffer;
	bool bCullingResultAvailable = false;
	bool bSupportVoxelization = true;

	/* CPU LOD selection. Hair LOD selection can be done by CPU or GPU. If bUseCPULODSelection is true, 
	   CPU LOD selection is enabled otherwise the GPU selection is used. CPU LOD selection use the CPU 
	   bounding box, which might not be as accurate as the GPU ones*/
	TArray<bool> LODVisibilities;
	TArray<float> LODScreenSizes;
	TArray<EHairGeometryType> LODGeometryTypes;

	// Data change every frame by the groom proxy based on views data
	float LODIndex = 0;			// Current LOD used for all views
	float LODBias = 0;			// Current LOD bias
	bool bLODVisibility = true; // Enable/disable hair rendering for this component
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster information exchanged between renderer and the hair strand plugin. 

struct FHairStrandClusterData
{
	struct FHairGroup
	{
		uint32 ClusterCount = 0;
		uint32 VertexCount = 0;

		float LODIndex = -1;
		float LODBias = 0.0f;
		bool bVisible = false;

		// See FHairStrandsClusterCullingResource fro details about those buffers.
		FRDGExternalBuffer* GroupAABBBuffer = nullptr;
		FRDGExternalBuffer* ClusterAABBBuffer = nullptr;
		FRDGExternalBuffer* ClusterInfoBuffer = nullptr;
		FRDGExternalBuffer* ClusterLODInfoBuffer = nullptr;
		FRDGExternalBuffer* VertexToClusterIdBuffer = nullptr;
		FRDGExternalBuffer* ClusterVertexIdBuffer = nullptr;

		TRefCountPtr<FRDGPooledBuffer> ClusterIdBuffer;
		TRefCountPtr<FRDGPooledBuffer> ClusterIndexOffsetBuffer;
		TRefCountPtr<FRDGPooledBuffer> ClusterIndexCountBuffer;

		// Culling & LOD output
		FRDGExternalBuffer* GetCulledVertexIdBuffer() const				{ return HairGroupPublicPtr ? &HairGroupPublicPtr->GetCulledVertexIdBuffer() : nullptr; }
		FRDGExternalBuffer* GetCulledVertexRadiusScaleBuffer() const	{ return HairGroupPublicPtr ? &HairGroupPublicPtr->GetCulledVertexRadiusScaleBuffer() : nullptr; }
		bool GetCullingResultAvailable() const							{ return HairGroupPublicPtr ? HairGroupPublicPtr->GetCullingResultAvailable() : false; }
		void SetCullingResultAvailable(bool b)							{ if (HairGroupPublicPtr) HairGroupPublicPtr->SetCullingResultAvailable(b); }

		TRefCountPtr<FRDGPooledBuffer> ClusterDebugInfoBuffer;							// Null if this debug is not enabled.
		TRefCountPtr<FRDGPooledBuffer> CulledDispatchIndirectParametersClusterCount;	// Null if this debug is not enabled.

		FHairGroupPublicData* HairGroupPublicPtr = nullptr;
	};

	TArray<FHairGroup> HairGroups;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

// API for enabling/disabling the various geometry representation
enum class EHairStrandsShaderType
{
	Strands,
	Cards,
	Meshes,
	Tool,
	All
};
RENDERER_API bool IsHairStrandsSupported(EHairStrandsShaderType Type, EShaderPlatform Platform);
RENDERER_API bool IsHairStrandsEnabled(EHairStrandsShaderType Type, EShaderPlatform Platform = EShaderPlatform::SP_NumPlatforms);
RENDERER_API void SetHairStrandsEnabled(bool In);

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

// Return true if the hair should be rendered using the sub-pixel lighting path, false if the regular gbuffer lighting path should be used
RENDERER_API bool IsHairStrandsComplexLightingEnabled();

// Return true if hair simulation is enabled.
RENDERER_API bool IsHairStrandsSimulationEnable();

// Return true if hair binding is enabled (i.e., hair can be attached to skeletal mesh)
RENDERER_API bool IsHairStrandsBindingEnable();

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef TArray<FRHIUnorderedAccessView*> FBufferTransitionQueue;
RENDERER_API void TransitBufferToReadable(FRDGBuilder& GraphBuilder, FBufferTransitionQueue& BuffersToTransit);

/// Return the hair coverage for a certain hair count and normalized avg hair radius (i.e, [0..1])
RENDERER_API float GetHairCoverage(uint32 HairCount, float AverageHairRadius);

/// Return the average hair normalized radius for a given hair count and a given coverage value
RENDERER_API float GetHairAvgRadius(uint32 InCount, float InCoverage);

/// Helper to enable debug information about hair LOD
RENDERER_API void SetHairScreenLODInfo(bool bEnable);


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HairStrands Bookmark API
enum class EHairStrandsBookmark : uint8
{
	ProcessTasks,
	ProcessLODSelection,
	ProcessGuideInterpolation,
	ProcessGatherCluster,
	ProcessStrandsInterpolation,
	ProcessDebug,
	ProcessEndOfFrame
};

struct FHairStrandsBookmarkParameters
{
	class FGPUSkinCache* SkinCache = nullptr;
	FShaderDrawDebugData* DebugShaderData = nullptr;
	EWorldType::Type WorldType = EWorldType::None;
	class FGlobalShaderMap* ShaderMap = nullptr;

	FIntRect ViewRect; // View 0
	const FSceneView* View = nullptr;// // View 0
	TArray<const FSceneView*> AllViews;
	TRefCountPtr<IPooledRenderTarget> SceneColorTexture = nullptr;

	bool bHzbRequest = false;
	bool bHasElements = false;
	bool bStrandsGeometryEnabled = false;
	uint32 FrameIndex = ~0;

	// Temporary
	FHairStrandClusterData HairClusterData;
};

typedef void (*THairStrandsParameterFunction)(FHairStrandsBookmarkParameters& Parameters);
typedef void (*THairStrandsBookmarkFunction)(FRDGBuilder& GraphBuilder, EHairStrandsBookmark Bookmark, FHairStrandsBookmarkParameters& Parameters);
RENDERER_API void RegisterBookmarkFunction(THairStrandsBookmarkFunction Bookmark, THairStrandsParameterFunction Parameters);
