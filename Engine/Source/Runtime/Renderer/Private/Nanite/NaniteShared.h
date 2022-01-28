// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "GlobalShader.h"
#include "UnifiedBuffer.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "Rendering/NaniteResources.h"
#include "NaniteFeedback.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNanite, Warning, All);

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteDebug, TEXT("Nanite Debug"));

struct FSceneTextures;
struct FDBufferTextures;

namespace Nanite
{

// Must match FStats in NaniteDataDecode.ush
struct FNaniteStats
{
	uint32 NumTris;
	uint32 NumVerts;
	uint32 NumViews;
	uint32 NumMainInstancesPreCull;
	uint32 NumMainInstancesPostCull;
	uint32 NumMainVisitedNodes;
	uint32 NumMainCandidateClusters;
	uint32 NumPostInstancesPreCull;
	uint32 NumPostInstancesPostCull;
	uint32 NumPostVisitedNodes;
	uint32 NumPostCandidateClusters;
	uint32 NumLargePageRectClusters;
	uint32 NumPrimaryViews;
	uint32 NumTotalViews;
};

struct FPackedView
{
	FMatrix44f	SVPositionToTranslatedWorld;
	FMatrix44f	ViewToTranslatedWorld;

	FMatrix44f	TranslatedWorldToView;
	FMatrix44f	TranslatedWorldToClip;
	FMatrix44f	ViewToClip;
	FMatrix44f	ClipToRelativeWorld;

	FMatrix44f	PrevTranslatedWorldToView;
	FMatrix44f	PrevTranslatedWorldToClip;
	FMatrix44f	PrevViewToClip;
	FMatrix44f	PrevClipToRelativeWorld;

	FIntVector4	ViewRect;
	FVector4f	ViewSizeAndInvSize;
	FVector4f	ClipSpaceScaleOffset;
	FVector4f	PreViewTranslation;
	FVector4f	PrevPreViewTranslation;
	FVector4f	WorldCameraOrigin;
	FVector4f	ViewForwardAndNearPlane;

	FVector3f	ViewTilePosition;
	uint32		Padding0;

	FVector3f	MatrixTilePosition;
	uint32		Padding1;

	FVector2f	LODScales;
	float		MinBoundsRadiusSq;
	uint32		StreamingPriorityCategory_AndFlags;

	FIntVector4 TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ;

	FIntVector4	HZBTestViewRect;	// In full resolution

	/**
	 * Calculates the LOD scales assuming view size and projection is already set up.
	 * TODO: perhaps more elegant/robust if this happened at construction time, and input was a non-packed NaniteView.
	 * Note: depends on the global 'GNaniteMaxPixelsPerEdge'.
	 */
	void UpdateLODScales();
};

struct FPackedViewParams
{
	FViewMatrices ViewMatrices;
	FViewMatrices PrevViewMatrices;
	FIntRect ViewRect;
	FIntPoint RasterContextSize;
	uint32 StreamingPriorityCategory = 0;
	float MinBoundsRadius = 0.0f;
	float LODScaleFactor = 1.0f;
	uint32 Flags = 0;

	int32 TargetLayerIndex = 0;
	int32 PrevTargetLayerIndex = INDEX_NONE;
	int32 TargetMipLevel = 0;
	int32 TargetMipCount = 1;

	FIntRect HZBTestViewRect = {0, 0, 0, 0};
};

FPackedView CreatePackedView(const FPackedViewParams& Params);

// Convenience function to pull relevant packed view parameters out of a FViewInfo
FPackedView CreatePackedViewFromViewInfo(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
	uint32 StreamingPriorityCategory = 0,
	float MinBoundsRadius = 0.0f,
	float LODScaleFactor = 1.0f
);

struct FVisualizeResult
{
	FRDGTextureRef ModeOutput;
	FName ModeName;
	int32 ModeID;
	uint8 bCompositeScene : 1;
	uint8 bSkippedTile    : 1;
};

/*
 * GPU side buffers containing Nanite resource data.
 */
class FGlobalResources : public FRenderResource
{
public:
	struct PassBuffers
	{
		// Used for statistics
		TRefCountPtr<FRDGPooledBuffer> StatsRasterizeArgsSWHWBuffer;
	};

	// Used for statistics
	uint32 StatsRenderFlags = 0;
	uint32 StatsDebugFlags = 0;

public:
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	void	Update(FRDGBuilder& GraphBuilder); // Called once per frame before any Nanite rendering has occurred.

	static uint32 GetMaxCandidateClusters();
	static uint32 GetMaxClusterBatches();
	static uint32 GetMaxVisibleClusters();
	static uint32 GetMaxNodes();

	inline PassBuffers& GetMainPassBuffers() { return MainPassBuffers; }
	inline PassBuffers& GetPostPassBuffers() { return PostPassBuffers; }

	TRefCountPtr<FRDGPooledBuffer>& GetMainAndPostNodesAndClusterBatchesBuffer() { return MainAndPostNodesAndClusterBatchesBuffer; };

	TRefCountPtr<FRDGPooledBuffer>& GetStatsBufferRef() { return StatsBuffer; }
	TRefCountPtr<FRDGPooledBuffer>& GetStructureBufferStride8() { return StructureBufferStride8; }

#if !UE_BUILD_SHIPPING
	FFeedbackManager* GetFeedbackManager() { return FeedbackManager; }
#endif
private:
	PassBuffers MainPassBuffers;
	PassBuffers PostPassBuffers;

	TRefCountPtr<FRDGPooledBuffer> MainAndPostNodesAndClusterBatchesBuffer;

	// Used for statistics
	TRefCountPtr<FRDGPooledBuffer> StatsBuffer;

	// Dummy structured buffer with stride8
	TRefCountPtr<FRDGPooledBuffer> StructureBufferStride8;

#if !UE_BUILD_SHIPPING
	FFeedbackManager* FeedbackManager = nullptr;
#endif
};

extern TGlobalResource< FGlobalResources > GGlobalResources;
} // namespace Nanite

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, )
	SHADER_PARAMETER(FIntVector4,					PageConstants)
	SHADER_PARAMETER(FIntVector4,					MaterialConfig) // .x mode, .yz grid size, .w unused
	SHADER_PARAMETER(uint32,						MaxNodes)
	SHADER_PARAMETER(uint32,						MaxVisibleClusters)
	SHADER_PARAMETER(uint32,						RenderFlags)
	SHADER_PARAMETER(FVector4f,						RectScaleOffset) // xy: scale, zw: offset
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			ClusterPageData)
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			VisibleClustersSWHW)
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			HierarchyBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>,	MaterialTileRemap)
	SHADER_PARAMETER_TEXTURE(Texture2D<UlongType>,	VisBuffer64)
	SHADER_PARAMETER_TEXTURE(Texture2D<UlongType>,	DbgBuffer64)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>,		DbgBuffer32)
	// Multi view
	SHADER_PARAMETER(uint32,									MultiViewEnabled)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>,				MultiViewIndices)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>,				MultiViewRectScaleOffsets)
	SHADER_PARAMETER_SRV(StructuredBuffer<FPackedNaniteView>,	InViews)
END_SHADER_PARAMETER_STRUCT()

class FNaniteGlobalShader : public FGlobalShader
{
public:
	FNaniteGlobalShader() = default;
	FNaniteGlobalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
	{
	}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}
};

class FNaniteMaterialShader : public FMaterialShader
{
public:
	FNaniteMaterialShader() = default;
	FNaniteMaterialShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
	{
	}

	static bool RequiresProgrammableVertex(const FMaterialShaderPermutationParameters& Parameters)
	{
	#if 0 // TODO: PROG_RASTER
		return Parameters.MaterialParameters.bMaterialMayModifyMeshPosition;
	#else
		return false;
	#endif
	}

	static bool RequiresProgrammablePixel(const FMaterialShaderPermutationParameters& Parameters)
	{
		const bool bProgrammablePixel =
		(
			Parameters.MaterialParameters.bIsMasked 
		#if 0 // TODO: PROG_RASTER
			|| Parameters.MaterialParameters.bHasPixelDepthOffsetConnected
			|| Parameters.MaterialParameters.bMaterialMayModifyMeshPosition
		#endif
		);

		return bProgrammablePixel;
	}

	static bool ShouldCompilePixelPermutation(const FMaterialShaderPermutationParameters& Parameters, bool bProgrammableRaster)
	{
		// Always compile default material as the fast opaque "fixed function" raster path
		bool bValidMaterial = Parameters.MaterialParameters.bIsDefaultMaterial;

		// Compile this pixel shader if it requires programmable raster and it's enabled
		if (bProgrammableRaster && Parameters.MaterialParameters.bIsUsedWithNanite && RequiresProgrammablePixel(Parameters))
		{
			bValidMaterial = true;
		}

		return
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			bValidMaterial;
	}
	
	static bool ShouldCompileVertexPermutation(const FMaterialShaderPermutationParameters& Parameters, bool bProgrammableRaster)
	{
		// Always compile default material as the fast opaque "fixed function" raster path
		bool bValidMaterial = Parameters.MaterialParameters.bIsDefaultMaterial;

		// Compile this vertex shader if it requires programmable raster and it's enabled
		if (bProgrammableRaster && Parameters.MaterialParameters.bIsUsedWithNanite && RequiresProgrammableVertex(Parameters))
		{
			bValidMaterial = true;
		}

		return
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			bValidMaterial;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_MATERIAL_SHADER"), 1);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		OutEnvironment.SetDefine(TEXT("IS_NANITE_RASTER_PASS"), 1);
		OutEnvironment.SetDefine(TEXT("IS_NANITE_PASS"), 1);
		OutEnvironment.SetDefine(TEXT("USE_ANALYTIC_DERIVATIVES"), 1);

		OutEnvironment.SetDefine(TEXT("NANITE_USE_UNIFORM_BUFFER"), 0);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_VIEW_UNIFORM_BUFFER"), 0);

		// Force definitions of GetObjectWorldPosition(), etc..
		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);
	}
};

extern bool ShouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport = true);

/** Checks whether Nanite would be rendered in this view. Used to give a visual warning about the project settings that can disable Nanite. */
extern bool WouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport = true, bool bCheckForProjectSetting = true);

extern bool UseComputeDepthExport();
