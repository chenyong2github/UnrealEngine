// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
VirtualShadowMapClipmap.cpp
=============================================================================*/

#include "VirtualShadowMapClipmap.h"
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RendererModule.h"
#include "VirtualShadowMapArray.h"
#include "VirtualShadowMapCacheManager.h"

static TAutoConsoleVariable<float> CVarVirtualShadowMapResolutionLodBiasDirectional(
	TEXT( "r.Shadow.Virtual.ResolutionLodBiasDirectional" ),
	-0.5f,
	TEXT( "Bias applied to LOD calculations for directional lights. -1.0 doubles resolution, 1.0 halves it and so on." ),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<int32> CVarVirtualShadowMapClipmapFirstLevel(
	TEXT( "r.Shadow.Virtual.Clipmap.FirstLevel" ),
	6,
	TEXT( "First level of the virtual clipmap. Lower values allow higher resolution shadows closer to the camera." ),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<int32> CVarVirtualShadowMapClipmapLastLevel(
	TEXT( "r.Shadow.Virtual.Clipmap.LastLevel" ),
	22,
	TEXT( "Last level of the virtual climap. Indirectly determines radius the clipmap can cover." ),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarVirtualShadowMapClipmapFirstCoarseLevel(
	TEXT("r.Shadow.Virtual.Clipmap.FirstCoarseLevel"),
	15,
	TEXT("First level of the clipmap to mark coarse pages for. Lower values allow higher resolution coarse pages near the camera but increase total page counts."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarVirtualShadowMapClipmapLastCoarseLevel(
	TEXT("r.Shadow.Virtual.Clipmap.LastCoarseLevel"),
	18,
	TEXT("Last level of the clipmap to mark coarse pages for. Higher values provide dense clipmap data for a longer radius but increase total page counts."),
	ECVF_RenderThreadSafe
);

// "Virtual" clipmap level to clipmap radius
// NOTE: This is the radius of around the clipmap origin that this level must cover
// The actual clipmap dimensions will be larger due to snapping and other accomodations
static float GetLevelRadius(int32 Level)
{
	// NOTE: Virtual clipmap indices can be negative (although not commonly)
	// Clipmap level rounds *down*, so radius needs to cover out to 2^(Level+1), where it flips
	return FMath::Pow(2.0f, static_cast<float>(Level + 1));
}

FVirtualShadowMapClipmap::FVirtualShadowMapClipmap(
	FVirtualShadowMapArray& VirtualShadowMapArray,
	FVirtualShadowMapArrayCacheManager* VirtualShadowMapArrayCacheManager,
	const FLightSceneInfo& InLightSceneInfo,
	const FMatrix& WorldToLightRotationMatrix,
	const FViewMatrices& CameraViewMatrices,
	FIntPoint CameraViewRectSize,
	const FViewInfo* InDependentView)
	: LightSceneInfo(InLightSceneInfo),
	  DependentView(InDependentView)
{
	check(WorldToLightRotationMatrix.GetOrigin() == FVector(0, 0, 0));	// Should not contain translation or scaling

	const bool bCacheValid = VirtualShadowMapArrayCacheManager && VirtualShadowMapArrayCacheManager->IsValid();

	const FMatrix FaceMatrix(
		FPlane( 0, 0, 1, 0 ),
		FPlane( 0, 1, 0, 0 ),
		FPlane(-1, 0, 0, 0 ),
		FPlane( 0, 0, 0, 1 ));

	WorldToViewRotationMatrix = WorldToLightRotationMatrix * FaceMatrix;
	// Pure rotation matrix
	FMatrix ViewToWorldRotationMatrix = WorldToViewRotationMatrix.GetTransposed();
	
	// NOTE: Rotational (roll) invariance of the directional light depends on square pixels so we just base everything on the camera X scales/resolution
	// NOTE: 0.5 because we double the size of the clipmap region below to handle snapping
	float LodScale = 0.5f / CameraViewMatrices.GetProjectionScale().X;
	LodScale *= float(FVirtualShadowMap::VirtualMaxResolutionXY) / float(CameraViewRectSize.X);
	
	// For now we adjust resolution by just biasing the page we look up in. This is wasteful in terms of page table vs.
	// just resizing the virtual shadow maps for each clipmap, but convenient for now. This means we need to additionally bias
	// which levels are present.
	ResolutionLodBias = CVarVirtualShadowMapResolutionLodBiasDirectional.GetValueOnRenderThread() + FMath::Log2(LodScale);
	// Clamp negative absolute resolution biases as they would exceed the maximum resolution/ranges allocated
	ResolutionLodBias = FMath::Max(0.0f, ResolutionLodBias);

	FirstLevel = CVarVirtualShadowMapClipmapFirstLevel.GetValueOnRenderThread();
	int32 LastLevel = CVarVirtualShadowMapClipmapLastLevel.GetValueOnRenderThread();
	LastLevel = FMath::Max(FirstLevel, LastLevel);
	int32 LevelCount = LastLevel - FirstLevel + 1;

	// Per-clipmap projection data
	LevelData.Empty();
	LevelData.AddDefaulted(LevelCount);

	WorldOrigin = CameraViewMatrices.GetViewOrigin();

	for (int32 Index = 0; Index < LevelCount; ++Index)
	{
		FLevelData& Level = LevelData[Index];
		const int32 AbsoluteLevel = Index + FirstLevel;		// Absolute (virtual) level index

		// TODO: Allocate these as a chunk if we continue to use one per clipmap level
		Level.VirtualShadowMap = VirtualShadowMapArray.Allocate();
		ensure(Index == 0 || (Level.VirtualShadowMap->ID == (LevelData[Index-1].VirtualShadowMap->ID + 1)));

		const float RawLevelRadius = GetLevelRadius(AbsoluteLevel);

		float HalfLevelDim = 2.0f * RawLevelRadius;
		float SnapSize = RawLevelRadius;

		FVector ViewCenter = WorldToViewRotationMatrix.TransformPosition(WorldOrigin);
		FIntPoint CenterSnapUnits(
			FMath::RoundToInt(ViewCenter.X / SnapSize),
			FMath::RoundToInt(ViewCenter.Y / SnapSize));
		ViewCenter.X = CenterSnapUnits.X * SnapSize;
		ViewCenter.Y = CenterSnapUnits.Y * SnapSize;

		FIntPoint CornerOffset;
		CornerOffset.X = -CenterSnapUnits.X + 2;
		CornerOffset.Y =  CenterSnapUnits.Y + 2;

		const FVector SnappedWorldCenter = ViewToWorldRotationMatrix.TransformPosition(ViewCenter);

		Level.WorldCenter = SnappedWorldCenter;
		Level.CornerOffset = CornerOffset;

		// Check if we have a cache entry for this level
		// If we do and it covers our required depth range, we can use cached pages. Otherwise we need to invalidate.
		TSharedPtr<FVirtualShadowMapCacheEntry> CacheEntry = nullptr;
		if (bCacheValid)
		{
			// NOTE: We use the absolute (virtual) level index so that the caching is robust against changes to the chosen level range
			CacheEntry = VirtualShadowMapArrayCacheManager->FindCreateCacheEntry(LightSceneInfo.Id, AbsoluteLevel);
		}

		// We expand the depth range of the clipmap level to allow for camera movement without having to invalidate cached shadow data
		// (See VirtualShadowMapCacheManager::UpdateClipmap for invalidation logic.)
		// Hard-coded constant currently; should probably always be at least 2.0f, otherwise the cache will be frequently invalidated.
		const float ViewRadiusZMultiplier = 5.0f;

		float ViewRadiusZ = RawLevelRadius * ViewRadiusZMultiplier;
		float ViewCenterDeltaZ = 0.0f;

		if (CacheEntry)
		{
			// We snap to half the size of the VSM at each level
			check((FVirtualShadowMap::Level0DimPagesXY & 1) == 0);
			FIntPoint PageOffset(CornerOffset * (FVirtualShadowMap::Level0DimPagesXY >> 2));

			CacheEntry->UpdateClipmap(Level.VirtualShadowMap->ID,
				WorldToLightRotationMatrix,
				PageOffset,
				RawLevelRadius,
				ViewCenter.Z,
				ViewRadiusZ);

			Level.VirtualShadowMap->VirtualShadowMapCacheEntry = CacheEntry;

			// Update min/max Z based on the cached page (if present and valid)
			// We need to ensure we use a consistent depth range as the camera moves for each level
			ViewCenterDeltaZ = ViewCenter.Z - CacheEntry->Clipmap.ViewCenterZ;
			ViewRadiusZ = CacheEntry->Clipmap.ViewRadiusZ;
		}

		const float ZScale = 0.5f / ViewRadiusZ;
		const float ZOffset = ViewRadiusZ + ViewCenterDeltaZ;
		Level.ViewToClip = FReversedZOrthoMatrix(HalfLevelDim, HalfLevelDim, ZScale, ZOffset);
	}
}

float FVirtualShadowMapClipmap::GetMaxRadius() const
{
	return GetLevelRadius(GetClipmapLevel(GetLevelCount() - 1));
}

FViewMatrices FVirtualShadowMapClipmap::GetViewMatrices(int32 ClipmapIndex) const
{
	check(ClipmapIndex >= 0 && ClipmapIndex < LevelData.Num());
	const FLevelData& Level = LevelData[ClipmapIndex];

	FViewMatrices::FMinimalInitializer Initializer;

	// NOTE: Be careful here! There's special logic in FViewMatrices around ViewOrigin for ortho projections we need to bypass...
	// There's also the fact that some of this data is going to be "wrong", due to the "overridden" matrix thing that shadows do
	Initializer.ViewOrigin = Level.WorldCenter;
	Initializer.ViewRotationMatrix = WorldToViewRotationMatrix;
	Initializer.ProjectionMatrix = Level.ViewToClip;

	// TODO: This is probably unused in the shadows/nanite path, but coupling here is not ideal
	Initializer.ConstrainedViewRect = FIntRect(0, 0, FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY);

	return FViewMatrices(Initializer);
}

FVirtualShadowMapProjectionShaderData FVirtualShadowMapClipmap::GetProjectionShaderData(int32 ClipmapIndex) const
{
	check(ClipmapIndex >= 0 && ClipmapIndex < LevelData.Num());
	const FLevelData& Level = LevelData[ClipmapIndex];
	
	// NOTE: Some shader logic (projection, etc) assumes some of these parameters are constant across all levels in a clipmap
	FVirtualShadowMapProjectionShaderData Data;
	Data.TranslatedWorldToShadowViewMatrix = WorldToViewRotationMatrix;
	Data.ShadowViewToClipMatrix = Level.ViewToClip;
	Data.TranslatedWorldToShadowUVMatrix = CalcTranslatedWorldToShadowUVMatrix(WorldToViewRotationMatrix, Level.ViewToClip);
	Data.TranslatedWorldToShadowUVNormalMatrix = CalcTranslatedWorldToShadowUVNormalMatrix(WorldToViewRotationMatrix, Level.ViewToClip);
	Data.ShadowPreViewTranslation = -Level.WorldCenter;
	Data.VirtualShadowMapId = Level.VirtualShadowMap->ID;
	Data.LightType = ELightComponentType::LightType_Directional;
	Data.ClipmapWorldOrigin = WorldOrigin;
	Data.ClipmapIndex = ClipmapIndex;
	Data.ClipmapLevel = FirstLevel + ClipmapIndex;
	Data.ClipmapLevelCount = LevelData.Num();
	Data.ClipmapResolutionLodBias = ResolutionLodBias;
	Data.ClipmapCornerOffset = Level.CornerOffset;

	return Data;
}

uint32 FVirtualShadowMapClipmap::GetCoarsePageClipmapIndexMask()
{
	uint32 BitMask = 0;

	const int FirstLevel = CVarVirtualShadowMapClipmapFirstLevel.GetValueOnRenderThread();
	const int LastLevel  = FMath::Max(FirstLevel, CVarVirtualShadowMapClipmapLastLevel.GetValueOnRenderThread());	
	int FirstCoarseIndex = CVarVirtualShadowMapClipmapFirstCoarseLevel.GetValueOnRenderThread() - FirstLevel;
	int LastCoarseIndex  = CVarVirtualShadowMapClipmapLastCoarseLevel.GetValueOnRenderThread() - FirstLevel;	

	ensureMsgf((LastLevel - FirstLevel) < 32, TEXT("Too many clipmap levels for coarse page bitmask."));

	FirstCoarseIndex = FMath::Max(0, FirstCoarseIndex);
	if (LastCoarseIndex >= FirstCoarseIndex)
	{
		uint32 BitCount = static_cast<uint32>(LastCoarseIndex - FirstCoarseIndex + 1);
		uint32 BitRange = (1 << BitCount) - 1;
		BitMask = BitMask | (BitRange << FirstCoarseIndex);
	}

	// Always mark coarse pages in the last level for clouds/skyatmosphere
	BitMask = BitMask | (1 << (LastLevel - FirstLevel));

	return BitMask;
}
