// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Shader.h"

class FPrimitiveSceneProxy;
class FViewInfo;
struct FMeshBatch;

////////////////////////////////////////////////////////////////////////////////////
// HairStrands uniform buffer

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairStrandsViewUniformParameters, )	
//	SHADER_PARAMETER(uint32, SampleCount)
	SHADER_PARAMETER(FIntPoint, HairTileCountXY)										// Tile count in X/Y
	SHADER_PARAMETER(float, HairDualScatteringRoughnessOverride)						// Override the roughness used for dual scattering (for hack/test purpose only)
	SHADER_PARAMETER(FIntPoint, HairSampleViewportResolution)							// Maximum viewport resolution of the sample space
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint4>, HairCategorizationTexture)			// Categorization texture aggregating hair info in screen space (closest depth, coverage, ...)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HairOnlyDepthTexture)				// Depth texture containing only hair depth
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HairSampleOffset)						// Offset & count, for accessing pixel's samples, based on screen pixel position
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HairSampleCount)						// Total count of hair sample, in sample space
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedHairSample>, HairSampleData)// Sample data (coverage, tangent, base color, ...), in sample space // HAIRSTRANDS_TODO: change this to be a uint4 so that we don't have to include the type for generated contant buffer
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, HairSampleCoords)					// Screen pixel coordinate of each sample, in sample space
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, HairTileData)						// Tile coords (RG16F)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>,  HairTileCount)						// Tile total count (actual number of tiles)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

////////////////////////////////////////////////////////////////////////////////////
// Tile data

struct FHairStrandsTiles
{
	FIntPoint			Resolution = FIntPoint(0, 0);
	static const uint32 GroupSize = 64;
	static const uint32	TileSize = 8;
	uint32				TileCount = 0;
	FIntPoint			TileCountXY = FIntPoint(0, 0);
	bool				bRectPrimitive = false;

	FRDGBufferSRVRef	TileDataSRV = nullptr;
	FRDGBufferRef		TileDataBuffer = nullptr;
	FRDGBufferRef		TileCountBuffer = nullptr;
	FRDGBufferRef		TileIndirectDrawBuffer = nullptr;
	FRDGBufferRef		TileIndirectDispatchBuffer = nullptr;

	bool IsValid() const { return TileCount > 0 && TileDataBuffer != nullptr; }
};

////////////////////////////////////////////////////////////////////////////////////
// Visibility Data

struct FHairStrandsVisibilityData
{
	FRDGTextureRef VelocityTexture = nullptr;
	FRDGTextureRef ResolveMaskTexture = nullptr;
	FRDGTextureRef CategorizationTexture = nullptr;
	FRDGTextureRef ViewHairCountTexture = nullptr;
	FRDGTextureRef ViewHairCountUintTexture = nullptr;
	FRDGTextureRef EmissiveTexture = nullptr;
	FRDGTextureRef HairOnlyDepthTexture = nullptr;

	FRDGTextureRef LightChannelMaskTexture = nullptr;

	uint32			MaxSampleCount = 8;
	uint32			MaxNodeCount = 0;
	FRDGTextureRef	NodeCount = nullptr;
	FRDGTextureRef	NodeIndex = nullptr;
	FRDGBufferRef	NodeData = nullptr;
	FRDGBufferRef	NodeCoord = nullptr;
	FRDGBufferRef	NodeIndirectArg = nullptr;
	uint32			NodeGroupSize = 0;

	FHairStrandsTiles TileData;

	const static EPixelFormat NodeCoordFormat = PF_R16G16_UINT;

	// Hair lighting is accumulated within this buffer
	// Allocated conservatively
	// User indirect dispatch for accumulating contribution
	FIntPoint SampleLightingViewportResolution;
	FRDGTextureRef SampleLightingBuffer = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////
// Voxel data

struct FHairStrandsVoxelNodeDesc
{
	FVector WorldMinAABB = FVector::ZeroVector;
	FVector WorldMaxAABB = FVector::ZeroVector;
	FIntVector PageIndexResolution = FIntVector::ZeroValue;
	FMatrix WorldToClip;
};

struct FPackedVirtualVoxelNodeDesc
{
	// This is just a placeholder having the correct size. The actual definition is in HairStradsNVoxelPageCommon.ush
	const static EPixelFormat Format = PF_R32G32B32A32_UINT;
	const static uint32 ComponentCount = 2;

	// Shader View is struct { uint4; uint4; }
	FVector	MinAABB;
	uint32	PackedPageIndexResolution;
	FVector	MaxAABB;
	uint32	PageIndexOffset;
};

// PixelRadiusAtDepth1 shouldn't be stored into this structure should be view independent, 
// but is put here for convenience at the moment since multiple views are not supported 
// at the moment
BEGIN_SHADER_PARAMETER_STRUCT(FHairStrandsVoxelCommonParameters, )
	SHADER_PARAMETER(FIntVector, PageCountResolution)
	SHADER_PARAMETER(float, VoxelWorldSize)
	SHADER_PARAMETER(FIntVector, PageTextureResolution)
	SHADER_PARAMETER(uint32, PageCount)
	SHADER_PARAMETER(uint32, PageResolution)
	SHADER_PARAMETER(uint32, PageIndexCount)
	SHADER_PARAMETER(uint32, IndirectDispatchGroupSize)
	SHADER_PARAMETER(uint32, NodeDescCount)

	SHADER_PARAMETER(float, DensityScale)
	SHADER_PARAMETER(float, DensityScale_AO)
	SHADER_PARAMETER(float, DensityScale_Shadow)
	SHADER_PARAMETER(float, DensityScale_Transmittance)
	SHADER_PARAMETER(float, DensityScale_Environment)
	SHADER_PARAMETER(float, DensityScale_Raytracing)

	SHADER_PARAMETER(float, DepthBiasScale_Shadow)
	SHADER_PARAMETER(float, DepthBiasScale_Transmittance)
	SHADER_PARAMETER(float, DepthBiasScale_Environment)

	SHADER_PARAMETER(float, SteppingScale_Shadow)
	SHADER_PARAMETER(float, SteppingScale_Transmittance)
	SHADER_PARAMETER(float, SteppingScale_Environment)
	SHADER_PARAMETER(float, SteppingScale_Raytracing)

	SHADER_PARAMETER(float, HairCoveragePixelRadiusAtDepth1)
	SHADER_PARAMETER(float, Raytracing_ShadowOcclusionThreshold)
	SHADER_PARAMETER(float, Raytracing_SkyOcclusionThreshold)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PageIndexBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, PageIndexOccupancyBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PageIndexCoordBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedVirtualVoxelNodeDesc>, NodeDescBuffer) // Packed into 2 x uint4
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualVoxelParameters, RENDERER_API)
	SHADER_PARAMETER_STRUCT_INCLUDE(FHairStrandsVoxelCommonParameters, Common)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, PageTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FHairStrandsVoxelResources
{
	FVirtualVoxelParameters	Parameters;
	TRDGUniformBufferRef<FVirtualVoxelParameters> UniformBuffer;
	FRDGTextureRef PageTexture = nullptr;
	FRDGBufferRef PageIndexBuffer = nullptr;
	FRDGBufferRef PageIndexOccupancyBuffer = nullptr;
	FRDGBufferRef NodeDescBuffer = nullptr;
	FRDGBufferRef PageIndexCoordBuffer = nullptr;
	FRDGBufferRef IndirectArgsBuffer = nullptr;
	FRDGBufferRef PageIndexGlobalCounter = nullptr;
	FRDGBufferRef VoxelizationViewInfoBuffer = nullptr;

	const bool IsValid() const { return UniformBuffer != nullptr && PageTexture != nullptr && NodeDescBuffer != nullptr; }
};


////////////////////////////////////////////////////////////////////////////////////
// Deep shadow data

struct FMinHairRadiusAtDepth1
{
	float Primary = 1;
	float Velocity = 1;
	float Stable = 1;
};

/// Hold deep shadow information for a given light.
struct FHairStrandsDeepShadowData
{
	static const uint32 MaxMacroGroupCount = 16u;

	FMatrix CPU_WorldToLightTransform;
	FMinHairRadiusAtDepth1 CPU_MinStrandRadiusAtDepth1;
	FIntRect AtlasRect;
	uint32 MacroGroupId = ~0;
	uint32 AtlasSlotIndex = 0;

	FIntPoint ShadowResolution = FIntPoint::ZeroValue;
	uint32 LightId = ~0;
	bool bIsLightDirectional = false;
	FVector  LightDirection;
	FVector4 LightPosition;
	FLinearColor LightLuminance;
	float LayerDistribution;

	FBoxSphereBounds Bounds;
};

typedef TArray<FHairStrandsDeepShadowData, SceneRenderingAllocator> FHairStrandsDeepShadowDatas;

struct FHairStrandsDeepShadowResources
{
	// Limit the number of atlas slot to 32, in order to create the view info per slot in single compute
	// This limitation can be alleviate, and is just here for convenience (see FDeepShadowCreateViewInfoCS)
	static const uint32 MaxAtlasSlotCount = 32u;

	uint32 TotalAtlasSlotCount = 0;
	FIntPoint AtlasSlotResolution;
	bool bIsGPUDriven = false;

	FRDGTextureRef DepthAtlasTexture = nullptr;
	FRDGTextureRef LayersAtlasTexture = nullptr;
	FRDGBufferRef DeepShadowWorldToLightTransforms = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////
// Cluster data

// A groom component contains one or several HairGroup. These hair group are send to the 
// render as mesh batches. These meshes batches are filtered/culled per view, and regroup 
// into HairMacroGroup for computing voxelization/DOM data, ...
//
// The hierarchy of the data structure is as follow:
//  * HairMacroGroup
//  * HairGroup
//  * HairCluster

struct FHairStrandsMacroGroupResources
{
	uint32 MacroGroupCount = 0;
	FRDGBufferRef MacroGroupAABBsBuffer = nullptr;
};

class FHairGroupPublicData;

/// Hair macro group infos
struct FHairStrandsMacroGroupData
{
	// List of primitive/mesh batch within an instance group
	struct PrimitiveInfo
	{
		const FMeshBatch* Mesh = nullptr;
		const FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;
		uint32 MaterialId;
		uint32 ResourceId;
		uint32 GroupIndex;
		FHairGroupPublicData* PublicDataPtr = nullptr;
		bool IsCullingEnable() const;
	};
	typedef TArray<PrimitiveInfo, SceneRenderingAllocator> TPrimitiveInfos;

	FHairStrandsVoxelNodeDesc VirtualVoxelNodeDesc;
	FHairStrandsDeepShadowDatas DeepShadowDatas;
	TPrimitiveInfos PrimitivesInfos;
	FBoxSphereBounds Bounds;
	FIntRect ScreenRect;
	uint32 MacroGroupId;

	bool bNeedScatterSceneLighting = false;
};

////////////////////////////////////////////////////////////////////////////////////
// Debug data

struct FHairStrandsDebugData
{
	BEGIN_SHADER_PARAMETER_STRUCT(FWriteParameters, )
		SHADER_PARAMETER(uint32, Debug_MaxShadingPointCount)
		SHADER_PARAMETER(uint32, Debug_MaxSampleCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, Debug_ShadingPointBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, Debug_ShadingPointCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, Debug_SampleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, Debug_SampleCounter)
		END_SHADER_PARAMETER_STRUCT()

		BEGIN_SHADER_PARAMETER_STRUCT(FReadParameters, )
		SHADER_PARAMETER(uint32, Debug_MaxShadingPointCount)
		SHADER_PARAMETER(uint32, Debug_MaxSampleCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Debug_ShadingPointBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Debug_ShadingPointCounter)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, Debug_SampleBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Debug_SampleCounter)
	END_SHADER_PARAMETER_STRUCT()

	struct ShadingInfo
	{
		FVector BaseColor;
		float	Roughness;
		FVector T;
		uint32	SampleCount;
		FVector V;
		float	SampleOffset;
	};

	struct Sample
	{
		FVector Direction;
		float	Pdf;
		FVector Weights;
		float	Pad;
	};
	static const uint32 MaxShadingPointCount = 32;
	static const uint32 MaxSampleCount = 1024 * 32;

	struct Data
	{
		FRDGBufferRef ShadingPointBuffer = nullptr;
		FRDGBufferRef ShadingPointCounter = nullptr;
		FRDGBufferRef SampleBuffer = nullptr;
		FRDGBufferRef SampleCounter = nullptr;
	} Resources;

	bool IsPlotDataValid() const
	{
		return Resources.ShadingPointBuffer && Resources.ShadingPointCounter && Resources.SampleBuffer && Resources.SampleCounter;
	}

	static Data CreateData(FRDGBuilder& GraphBuilder);
	static void SetParameters(FRDGBuilder& GraphBuilder, Data& In, FWriteParameters& Out);
	static void SetParameters(FRDGBuilder& GraphBuilder, const Data& In, FReadParameters& Out);

	// PPLL debug data
	bool IsPPLLDataValid() const 
	{ 
		return PPLLNodeCounterTexture && PPLLNodeIndexTexture && PPLLNodeDataBuffer; 
	}

	FRDGTextureRef	PPLLNodeCounterTexture = nullptr;
	FRDGTextureRef	PPLLNodeIndexTexture = nullptr;
	FRDGBufferRef	PPLLNodeDataBuffer = nullptr;
};

typedef TArray<FHairStrandsMacroGroupData, SceneRenderingAllocator> FHairStrandsMacroGroupDatas;

////////////////////////////////////////////////////////////////////////////////////
// View Data
struct FHairStrandsViewData
{
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> UniformBuffer;
	bool bIsValid = false;

	// Internal data
	FHairStrandsVisibilityData VisibilityData;
	FHairStrandsMacroGroupDatas MacroGroupDatas;
	FHairStrandsDeepShadowResources DeepShadowResources;
	FHairStrandsVoxelResources VirtualVoxelResources;
	FHairStrandsMacroGroupResources MacroGroupResources;
	FHairStrandsDebugData DebugData;
};

namespace HairStrands
{
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> CreateDefaultHairStrandsViewUniformBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View);
	TRDGUniformBufferRef<FHairStrandsViewUniformParameters> BindHairStrandsViewUniformParameters(const FViewInfo& View);
	TRDGUniformBufferRef<FVirtualVoxelParameters> BindHairStrandsVoxelUniformParameters(const FViewInfo& View);
	bool HasViewHairStrandsData(const FViewInfo& View);
	bool HasViewHairStrandsData(const TArrayView<FViewInfo>& Views);
	bool HasViewHairStrandsVoxelData(const FViewInfo& View);
}