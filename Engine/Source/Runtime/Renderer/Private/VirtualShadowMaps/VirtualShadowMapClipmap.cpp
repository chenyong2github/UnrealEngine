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

static TAutoConsoleVariable<float> CVarVirtualShadowMapClipmapResolutionLodBias(
	TEXT( "r.Shadow.v.Clipmap.ResolutionLodBias" ),
	-0.5f,
	TEXT( "" ),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<int32> CVarVirtualShadowMapClipmapFirstLevel(
	TEXT( "r.Shadow.v.Clipmap.FirstLevel" ),
	8,
	TEXT( "First level of the virtual clipmap. Lower values allow higher resolution shadows closer to the camera." ),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<float> CVarVirtualShadowMapClipmapMaxRadius(
	TEXT( "r.Shadow.v.Clipmap.MaxRadius" ),
	1000000.0f,
	TEXT( "Maximum distance the clipmap covers. Determines the number of clipmap levels. " ),
	ECVF_RenderThreadSafe
);

// "Virtual" clipmap level to clipmap radius
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

	const FMatrix FaceMatrix(
		FPlane( 0, 0, 1, 0 ),
		FPlane( 0, 1, 0, 0 ),
		FPlane(-1, 0, 0, 0 ),
		FPlane( 0, 0, 0, 1 ));

	WorldToViewRotationMatrix = WorldToLightRotationMatrix * FaceMatrix;
	// Pure rotation matrix
	FMatrix ViewToWorldRotationMatrix = WorldToViewRotationMatrix.GetTransposed();
	
	// NOTE: Rotational (roll) invariance of the directional light depends on square pixels so we just base everything on the camera X scales/resolution
	float LodScale = 1.0f / CameraViewMatrices.GetProjectionScale().X;
	LodScale *= float(FVirtualShadowMap::VirtualMaxResolutionXY) / float(CameraViewRectSize.X);
	
	ResolutionLodBias = CVarVirtualShadowMapClipmapResolutionLodBias.GetValueOnRenderThread() + FMath::Log2(LodScale);
	// Clamp negative absolute resolution biases as they would exceed the maximum resolution/ranges allocated
	ResolutionLodBias = FMath::Max(0.0f, ResolutionLodBias);

	// For now we adjust resolution by just biasing the page we look up in. This is wasteful in terms of page table vs.
	// just resizing the virtual shadow maps for each clipmap, but convenient for now. This means we need to additionally bias
	// which levels are present.
	FirstLevel = CVarVirtualShadowMapClipmapFirstLevel.GetValueOnRenderThread();
	MaxRadius  = CVarVirtualShadowMapClipmapMaxRadius.GetValueOnRenderThread();
	int32 LastLevel = FMath::FloorToInt(FMath::Log2(MaxRadius) + ResolutionLodBias);
	LastLevel = FMath::Max(FirstLevel, LastLevel);
	int32 LevelCount = LastLevel - FirstLevel + 1;

	// Per-clipmap projection data
	LevelData.Empty();
	LevelData.AddDefaulted(LevelCount);

	WorldOrigin = CameraViewMatrices.GetViewOrigin();

	for (int32 Index = 0; Index < LevelCount; ++Index)
	{
		FLevelData& Level = LevelData[Index];
		const int32 LevelIndex = Index + FirstLevel;		// Absolute (virtual) level index

		// TODO: Allocate these as a chunk if we continue to use one per clipmap level
		// This isn't actually required at the moment but enforcing it keeps optimization options open
		Level.VirtualShadowMap = VirtualShadowMapArray.Allocate();
		ensure(Index == 0 || (Level.VirtualShadowMap->ID == (LevelData[Index-1].VirtualShadowMap->ID + 1)));

		// Snap world center point to page boundaries
		// Max error from snapping with round to nearest is the diagonal sqrt(2) * PageSize
		// Must ensure radius is adjusted to cover the entire original radius
		constexpr float DimPages = static_cast<float>(FVirtualShadowMap::Level0DimPagesXY);
		const float RawLevelRadius = GetLevelRadius(LevelIndex);
		float PageSizeInWorldSpace = (2.0f * RawLevelRadius) / (DimPages - UE_SQRT_2);

		// NOTE: We may eventually want to snap/quantize Z as well, but right now we just adjust it each time
		// we update the cache page. With clipmaps this could ertainly be simplified.
		FVector ViewCenter = WorldToViewRotationMatrix.TransformPosition(WorldOrigin);
		FIntPoint PageSpaceCenter(
			FMath::RoundToInt(ViewCenter.X / PageSizeInWorldSpace),
			FMath::RoundToInt(ViewCenter.Y / PageSizeInWorldSpace));
		ViewCenter.X = PageSpaceCenter.X * PageSizeInWorldSpace;
		ViewCenter.Y = PageSpaceCenter.Y * PageSizeInWorldSpace;

		const FVector SnappedWorldCenter = ViewToWorldRotationMatrix.TransformPosition(ViewCenter);
		const float SnappedLevelRadius = 0.5f * (PageSizeInWorldSpace * DimPages);

		const float ZScale = 0.5f / RawLevelRadius;
		const float ZOffset = RawLevelRadius;

		Level.WorldCenter = SnappedWorldCenter;
		Level.ViewToClip = FReversedZOrthoMatrix(SnappedLevelRadius, SnappedLevelRadius, ZScale, ZOffset);

		if (VirtualShadowMapArrayCacheManager)
		{
			// NOTE: We use the absolute (virtual) level index so that the caching is robust against changes to the chosen level range
			TSharedPtr<FVirtualShadowMapCacheEntry> CacheEntry = VirtualShadowMapArrayCacheManager->FindCreateCacheEntry(LightSceneInfo.Id, LevelIndex);
			if (CacheEntry)
			{
				FIntPoint PageOffset(PageSpaceCenter);
				PageOffset.Y = -PageOffset.Y;		// Viewport
				float DepthOffset = -ViewCenter.Z * ZScale;

				CacheEntry->UpdateClipmap(Level.VirtualShadowMap->ID, WorldToLightRotationMatrix, PageOffset, DepthOffset);

				Level.VirtualShadowMap->VirtualShadowMapCacheEntry = CacheEntry;
			}
		}
	}
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
	Data.ClipmapLevel = FirstLevel + ClipmapIndex;
	Data.ClipmapLevelCount = LevelData.Num();
	Data.ClipmapResolutionLodBias = ResolutionLodBias;

	return Data;
}
