// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
StreamingTexture.cpp: Definitions of classes used for texture.
=============================================================================*/

#include "Streaming/StreamingTexture.h"
#include "Misc/App.h"
#include "Streaming/StreamingManagerTexture.h"
#include "HAL/FileManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"

FStreamingRenderAsset::FStreamingRenderAsset(
	UStreamableRenderAsset* InRenderAsset,
	const int32* NumStreamedMips,
	int32 NumLODGroups,
	EAssetType InAssetType,
	const FRenderAssetStreamingSettings& Settings)
	: RenderAsset(InRenderAsset)
	, RenderAssetType(InAssetType)
{
	UpdateStaticData(Settings);
	UpdateDynamicData(NumStreamedMips, NumLODGroups, Settings, false);

	InstanceRemovedTimestamp = FApp::GetCurrentTime();
	DynamicBoostFactor = 1.f;

	bHasUpdatePending = InRenderAsset && InRenderAsset->bHasStreamingUpdatePending;

	bForceFullyLoadHeuristic = false;
	bUseUnkownRefHeuristic = false;
	NumMissingMips = 0;
	bLooksLowRes = false;
	VisibleWantedMips = MinAllowedMips;
	HiddenWantedMips = MinAllowedMips;
	RetentionPriority = 0;
	BudgetedMips = MinAllowedMips;
	NumForcedMips = 0;
	LoadOrderPriority = 0;
	WantedMips = MinAllowedMips;
}

void FStreamingRenderAsset::UpdateStaticData(const FRenderAssetStreamingSettings& Settings)
{

	OptionalBulkDataFilename = TEXT("");
	if (RenderAsset)
	{
		LODGroup = RenderAsset->GetLODGroupForStreaming();
		NumNonStreamingMips = RenderAsset->GetNumNonStreamingMips();
		MipCount = RenderAsset->GetNumMipsForStreaming();
		BudgetMipBias = 0;

		if (IsTexture())
		{
			MipCount = FMath::Min<int32>(MipCount, MAX_TEXTURE_MIP_COUNT);
			const TextureGroup TextureLODGroup = static_cast<TextureGroup>(LODGroup);
			BoostFactor = GetExtraBoost(TextureLODGroup, Settings);
			bIsCharacterTexture = (TextureLODGroup == TEXTUREGROUP_Character || TextureLODGroup == TEXTUREGROUP_CharacterSpecular || TextureLODGroup == TEXTUREGROUP_CharacterNormalMap);
			bIsTerrainTexture = (TextureLODGroup == TEXTUREGROUP_Terrain_Heightmap || TextureLODGroup == TEXTUREGROUP_Terrain_Weightmap);
		}
		else
		{
			check(MipCount <= MaxNumMeshLODs);
			// Default boost value .71 is too small for meshes
			BoostFactor = 1.f;
			bIsCharacterTexture = false;
			bIsTerrainTexture = false;
			if (RenderAssetType == AT_StaticMesh)
			{
				const UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(RenderAsset);
				for (int32 Idx = 0; Idx < MaxNumMeshLODs; ++Idx)
				{
					const int32 LODIdx = FMath::Max(MipCount - Idx - 1, 0);
					// Screen sizes stored on assets are 2R/D where R is the radius of bounding spheres and D is the
					// distance from view origins to bounds origins. The factor calculated by the streamer, however,
					// is R/D so multiply 0.5 here
					LODScreenSizes[Idx] = StaticMesh->RenderData->ScreenSize[LODIdx].GetValueForFeatureLevel(GMaxRHIFeatureLevel) * 0.5f;
				}
			}
			else // AT_SkeletalMesh
			{
				USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(RenderAsset);
				const TArray<FSkeletalMeshLODInfo>& LODInfos =  SkeletalMesh->GetLODInfoArray();
				for (int32 Idx = 0; Idx < MaxNumMeshLODs; ++Idx)
				{
					const int32 LODIdx = FMath::Max(MipCount - Idx - 1, 0);
					LODScreenSizes[Idx] = LODInfos[LODIdx].ScreenSize.GetValueForFeatureLevel(GMaxRHIFeatureLevel) * 0.5f;
				}
			}
		}

		NumNonOptionalMips = MipCount - RenderAsset->CalcNumOptionalMips();
		OptionalMipsState = (NumNonOptionalMips == MipCount) ? EOptionalMipsState::OMS_NoOptionalMips : EOptionalMipsState::OMS_NotCached;

		const int32 MaxNumMips = IsTexture() ? MAX_TEXTURE_MIP_COUNT : MaxNumMeshLODs;
		for (int32 MipIndex = 0; MipIndex < MaxNumMips; ++MipIndex)
		{
			CumulativeLODSizes[MipIndex] = RenderAsset->CalcCumulativeLODSize(FMath::Min(MipIndex + 1, MipCount));
		}

		const int32 OptionalMipCount = MipCount - NumNonOptionalMips;
		const int32 OptionalMipIndex = OptionalMipCount - 1; // just here so it's clear why this -1 is here
		if (!RenderAsset->GetMipDataFilename(OptionalMipIndex, OptionalBulkDataFilename))
		{
			OptionalBulkDataFilename.Empty();
		}
	}
	else
	{
		LODGroup = TEXTUREGROUP_World;
		RenderAssetType = AT_Num;
		NumNonStreamingMips = 0;
		MipCount = 0;
		BudgetMipBias = 0;
		BoostFactor = 1.f;
		NumNonOptionalMips = MipCount;
		OptionalMipsState = EOptionalMipsState::OMS_NoOptionalMips;

		bIsCharacterTexture = false;
		bIsTerrainTexture = false;

		for (int32 MipIndex=0; MipIndex < MAX_TEXTURE_MIP_COUNT; ++MipIndex)
		{
			CumulativeLODSizes[MipIndex] = 0;
		}
	}
}

void FStreamingRenderAsset::UpdateOptionalMipsState_Async()
{
	// Here we do a lazy update where we check if the highres mip file exists only if it could be useful to do so.
	// This requires texture to be at max resolution before the optional mips .
	if (OptionalMipsState == EOptionalMipsState::OMS_NotCached && !OptionalBulkDataFilename.IsEmpty())
	{
		OptionalMipsState = IFileManager::Get().FileExists(*OptionalBulkDataFilename) ? EOptionalMipsState::OMS_HasOptionalMips : EOptionalMipsState::OMS_NoOptionalMips;
	}

}

void FStreamingRenderAsset::UpdateDynamicData(const int32* NumStreamedMips, int32 NumLODGroups, const FRenderAssetStreamingSettings& Settings, bool bWaitForMipFading)
{
	// Note that those values are read from the async task and must not be assigned temporary values!!
	if (RenderAsset)
	{
		UpdateStreamingStatus(bWaitForMipFading);

		// The last render time of this texture/mesh. Can be FLT_MAX when texture has no resource.
		const float LastRenderTimeForTexture = RenderAsset->GetLastRenderTimeForStreaming();
		LastRenderTime = (FApp::GetCurrentTime() > LastRenderTimeForTexture) ? float( FApp::GetCurrentTime() - LastRenderTimeForTexture ) : 0.0f;

		bForceFullyLoad = RenderAsset->ShouldMipLevelsBeForcedResident();

		bIgnoreStreamingMipBias = RenderAsset->bIgnoreStreamingMipBias;

		const int32 NumCinematicMipLevels = (bForceFullyLoad && RenderAsset->bUseCinematicMipLevels) ? RenderAsset->NumCinematicMipLevels : 0;

		int32 LODBias = 0;
		if (!Settings.bUseAllMips)
		{
			LODBias = FMath::Max<int32>(RenderAsset->GetCachedLODBias() - NumCinematicMipLevels, 0);


#if WITH_EDITORONLY_DATA
			// When data is not cooked, the asset can have more mips than the engine supports.
			// The engine limit is applied in UpdateStaticData() when computing MipCount, but this will also be be accounted in GetCachedLODBias().
			LODBias -= RenderAsset->GetNumMipsForStreaming() - MipCount;
#endif

			// Reduce the max allowed resolution according to LODBias if the texture group allows it.
			if (IsMaxResolutionAffectedByGlobalBias() && !Settings.bUsePerTextureBias)
			{
				LODBias += Settings.GlobalMipBias;
			}

			LODBias += BudgetMipBias;
		}

		// Update MaxAllowedMips in an atomic way to avoid possible bad interaction with the async task.
		{
			// The max mip count is affected by the texture bias and cinematic bias settings.
			// don't set MaxAllowdMips more then once as it could be read by async texture task
			int32 TempMaxAllowedMips = FMath::Clamp<int32>(FMath::Min<int32>(MipCount - LODBias, GMaxTextureMipCount), NumNonStreamingMips, MipCount);
			if (NumNonOptionalMips < MipCount)
			{
				// If the optional mips are not available, or if we shouldn't load them now, clamp the possible mips requested. 
				// (when the non-optional mips are not yet loaded, loading optional mips generates cross files requests).
				// This is not bullet proof though since the texture/mesh could have a pending stream-out request.
				if (OptionalMipsState != EOptionalMipsState::OMS_HasOptionalMips || ResidentMips < NumNonOptionalMips)
				{
					TempMaxAllowedMips = FMath::Min(TempMaxAllowedMips, NumNonOptionalMips);
				}
			}
			MaxAllowedMips = TempMaxAllowedMips;
		}
	
		check(LODGroup < NumLODGroups);
		if (NumStreamedMips[LODGroup] > 0)
		{
			MinAllowedMips = FMath::Clamp<int32>(MipCount - NumStreamedMips[LODGroup], NumNonStreamingMips, MaxAllowedMips);
		}
		else
		{
			MinAllowedMips = NumNonStreamingMips;
		}
	}
	else
	{
		bReadyForStreaming = false;
		bInFlight = false;
		bForceFullyLoad = false;
		bIgnoreStreamingMipBias = false;
		ResidentMips = 0;
		RequestedMips = 0;
		MinAllowedMips = 0;
		MaxAllowedMips = 0;
		NumNonOptionalMips = 0;
		OptionalMipsState = EOptionalMipsState::OMS_NotCached;
		LastRenderTime = FLT_MAX;	
	}
}

void FStreamingRenderAsset::UpdateStreamingStatus(bool bWaitForMipFading)
{
	if (RenderAsset)
	{
		bInFlight = RenderAsset->UpdateStreamingStatus(bWaitForMipFading);

		// Optimization: Use GetCachedNumResidentLODs() and GetCachedReadyForStreaming()
		// instead of GetNumResidentMips() and IsReadyForStreaming() to reduce cache misses
		// Platforms tested and results (ave exec time of FRenderAssetStreamingManager::UpdateResourceStreaming):
		//   PS4 Pro - from ~0.79 ms/frame to ~0.55 ms/frame

		// This must be updated after UpdateStreamingStatus
		ResidentMips = RenderAsset->GetCachedNumResidentLODs();
		if (!bReadyForStreaming)
		{
			bReadyForStreaming = RenderAsset->GetCachedReadyForStreaming();
		}
		RequestedMips = RenderAsset->GetNumRequestedMips();
	}
	else
	{
		bReadyForStreaming = false;
		bInFlight = false;
	}
}

float FStreamingRenderAsset::GetExtraBoost(TextureGroup	LODGroup, const FRenderAssetStreamingSettings& Settings)
{
	const float DistanceScale = GetDefaultExtraBoost(Settings.bUseNewMetrics);

	if (LODGroup == TEXTUREGROUP_Terrain_Heightmap || LODGroup == TEXTUREGROUP_Terrain_Weightmap) 
	{
		// Terrain are not affected by any kind of scale. Important since instance can use hardcoded resolution.
		// Used the Distance Scale from the new metrics is not big enough to affect which mip gets selected.
		return DistanceScale;
	}
	else if (LODGroup == TEXTUREGROUP_Lightmap)
	{
		return FMath::Min<float>(DistanceScale, GLightmapStreamingFactor);
	}
	else if (LODGroup == TEXTUREGROUP_Shadowmap)
	{
		return FMath::Min<float>(DistanceScale, GShadowmapStreamingFactor);
	}
	else
	{
		return DistanceScale;
	}
}

int32 FStreamingRenderAsset::GetWantedMipsFromSize(float Size, float MaxScreenSizeOverAllViews) const
{
	if (IsTexture())
	{
		float WantedMipsFloat = 1.0f + FMath::Log2(FMath::Max(1.f, Size));
		int32 WantedMipsInt = FMath::CeilToInt(WantedMipsFloat);
		return FMath::Clamp<int32>(WantedMipsInt, MinAllowedMips, MaxAllowedMips);
	}
	else
	{
		check(MinAllowedMips >= 1);
		check(MaxAllowedMips <= MipCount);
		check(RenderAssetType == AT_StaticMesh || RenderAssetType == AT_SkeletalMesh);
		if (Size != FLT_MAX)
		{
			const float NormalizedSize = Size / MaxScreenSizeOverAllViews;
			for (int32 NumMips = MinAllowedMips; NumMips <= MaxAllowedMips; ++NumMips)
			{
				if (GetNormalizedScreenSize(NumMips) >= NormalizedSize)
				{
					return NumMips;
				}
			}
		}
		return MaxAllowedMips;
	}
}

/** Set the wanted mips from the async task data */
void FStreamingRenderAsset::SetPerfectWantedMips_Async(
	float MaxSize,
	float MaxSize_VisibleOnly,
	float MaxScreenSizeOverAllViews,
	int32 MaxNumForcedLODs,
	bool InLooksLowRes,
	const FRenderAssetStreamingSettings& Settings)
{
	bForceFullyLoadHeuristic = (MaxSize == FLT_MAX || MaxSize_VisibleOnly == FLT_MAX);
	bLooksLowRes = InLooksLowRes; // Things like lightmaps, HLOD and close instances.

	if (MaxNumForcedLODs >= MaxAllowedMips)
	{
		VisibleWantedMips = HiddenWantedMips = NumForcedMips = MaxAllowedMips;
		NumMissingMips = 0;
		return;
	}

	NumForcedMips = FMath::Min(MaxNumForcedLODs, MaxAllowedMips);
	VisibleWantedMips = FMath::Max(GetWantedMipsFromSize(MaxSize_VisibleOnly, MaxScreenSizeOverAllViews), NumForcedMips);

	// Terrain, Forced Fully Load and Things that already look bad are not affected by hidden scale.
	if (bIsTerrainTexture || bForceFullyLoadHeuristic || bLooksLowRes)
	{
		HiddenWantedMips = FMath::Max(GetWantedMipsFromSize(MaxSize, MaxScreenSizeOverAllViews), NumForcedMips);
		NumMissingMips = 0; // No impact for terrains as there are not allowed to drop mips.
	}
	else
	{
		HiddenWantedMips = FMath::Max(GetWantedMipsFromSize(MaxSize * Settings.HiddenPrimitiveScale, MaxScreenSizeOverAllViews), NumForcedMips);
		// NumMissingMips contains the number of mips not loaded because of HiddenPrimitiveScale. When out of budget, those texture will be considered as already sacrificed.
		NumMissingMips = FMath::Max<int32>(GetWantedMipsFromSize(MaxSize, MaxScreenSizeOverAllViews) - FMath::Max<int32>(VisibleWantedMips, HiddenWantedMips), 0);
	}
}

/**
 * Once the wanted mips are computed, the async task will check if everything fits in the budget.
 *  This only consider the highest mip that will be requested eventually, so that slip requests are stables.
 */
int64 FStreamingRenderAsset::UpdateRetentionPriority_Async(bool bPrioritizeMesh)
{
	// Reserve the budget for the max mip that will be loaded eventually (ignore the effect of split requests)
	BudgetedMips = GetPerfectWantedMips();
	RetentionPriority = 0;

	if (RenderAsset)
	{
		const bool bIsHuge    = GetSize(BudgetedMips) >= 8 * 1024 * 1024 && LODGroup != TEXTUREGROUP_Lightmap && LODGroup != TEXTUREGROUP_Shadowmap;
		const bool bShouldKeep = bIsTerrainTexture || bForceFullyLoadHeuristic || (bLooksLowRes && !bIsHuge);
		const bool bIsSmall   = GetSize(BudgetedMips) <= 200 * 1024; 
		const bool bIsVisible = VisibleWantedMips >= HiddenWantedMips; // Whether the first mip dropped would be a visible mip or not.

		// Here we try to have a minimal amount of priority flags for last render time to be meaningless.
		// We mostly want thing not seen from a long time to go first to avoid repeating load / unload patterns.

		if (bPrioritizeMesh && IsMesh())		RetentionPriority += 4096; // Only consider meshes after textures are processed for faster metric calculation.
		if (bShouldKeep)						RetentionPriority += 2048; // Keep forced fully load as much as possible.
		if (bIsVisible)							RetentionPriority += 1024; // Keep visible things as much as possible.
		if (!bIsHuge)							RetentionPriority += 512; // Drop high resolution which usually target ultra close range quality.
		if (bIsCharacterTexture || bIsSmall)	RetentionPriority += 256; // Try to keep character of small texture as they don't pay off.
		if (!bIsVisible)						RetentionPriority += FMath::Clamp<int32>(255 - (int32)LastRenderTime, 1, 255); // Keep last visible first.

		return GetSize(BudgetedMips);
	}
	else
	{
		return 0;
	}
}

int32 FStreamingRenderAsset::ClampMaxResChange_Internal(int32 NumMipDropRequested) const
{
	// Don't drop bellow min allowed mips. Also ensure that MinAllowedMips < MaxAllowedMips in order allow the BudgetMipBias to reset.
	return FMath::Min(MaxAllowedMips - MinAllowedMips - 1, NumMipDropRequested);
}

int64 FStreamingRenderAsset::DropMaxResolution_Async(int32 NumDroppedMips)
{
	if (RenderAsset)
	{
		NumDroppedMips = ClampMaxResChange_Internal(NumDroppedMips);

		if (NumDroppedMips > 0)
		{
			// Decrease MaxAllowedMips and increase BudgetMipBias (as it should include it)
			MaxAllowedMips -= NumDroppedMips;
			BudgetMipBias += NumDroppedMips;

			if (BudgetedMips > MaxAllowedMips)
			{
				const int64 FreedMemory = GetSize(BudgetedMips) - GetSize(MaxAllowedMips);

				BudgetedMips = MaxAllowedMips;
				VisibleWantedMips = FMath::Min<int32>(VisibleWantedMips, MaxAllowedMips);
				HiddenWantedMips = FMath::Min<int32>(HiddenWantedMips, MaxAllowedMips);

				return FreedMemory;
			}
		}
		else // If we can't reduce resolution, still drop a mip if possible to free memory (eventhough it won't be persistent)
		{
			return DropOneMip_Async();
		}
	}
	return 0;
}

int64 FStreamingRenderAsset::DropOneMip_Async()
{
	if (RenderAsset && BudgetedMips > MinAllowedMips)
	{
		--BudgetedMips;
		return GetSize(BudgetedMips + 1) - GetSize(BudgetedMips);
	}
	else
	{
		return 0;
	}
}

int64 FStreamingRenderAsset::KeepOneMip_Async()
{
	if (RenderAsset && BudgetedMips < FMath::Min<int32>(ResidentMips, MaxAllowedMips))
	{
		++BudgetedMips;
		return GetSize(BudgetedMips) - GetSize(BudgetedMips - 1);
	}
	else
	{
		return 0;
	}
}

int64 FStreamingRenderAsset::GetDropMaxResMemDelta(int32 NumDroppedMips) const
{
	if (RenderAsset)
	{
		NumDroppedMips = ClampMaxResChange_Internal(NumDroppedMips);
		return GetSize(MaxAllowedMips) - GetSize(MaxAllowedMips - NumDroppedMips);
	}

	return 0;
}

int64 FStreamingRenderAsset::GetDropOneMipMemDelta() const
{
	return GetSize(BudgetedMips + 1) - GetSize(BudgetedMips);
}

bool FStreamingRenderAsset::UpdateLoadOrderPriority_Async(int32 MinMipForSplitRequest)
{
	LoadOrderPriority = 0;

	// First load the visible mips, then later load the non visible part (does not apply to terrain textures as distance fields update may be waiting).
	if (ResidentMips < VisibleWantedMips && VisibleWantedMips < BudgetedMips && BudgetedMips >= MinMipForSplitRequest && !bIsTerrainTexture)
	{
		WantedMips = VisibleWantedMips;
	}
	else
	{
		WantedMips = BudgetedMips;
	}

	// If the entry is valid and we need to send a new request to load/drop the right mip.
	if (bReadyForStreaming && RenderAsset && WantedMips != RequestedMips)
	{
		const bool bIsVisible			= ResidentMips < VisibleWantedMips; // Otherwise it means we are loading mips that are only useful for non visible primitives.
		const bool bMustLoadFirst		= bForceFullyLoadHeuristic || bIsTerrainTexture || bIsCharacterTexture;
		const bool bMipIsImportant		= WantedMips - ResidentMips > (bLooksLowRes ? 1 : 2);

		if (bIsVisible)				LoadOrderPriority += 1024;
		if (bMustLoadFirst)			LoadOrderPriority += 512; 
		if (bMipIsImportant)		LoadOrderPriority += 256;
		if (!bIsVisible)			LoadOrderPriority += FMath::Clamp<int32>(255 - (int32)LastRenderTime, 1, 255);

		return true;
	}
	else
	{
		return false;
	}
}

void FStreamingRenderAsset::CancelPendingMipChangeRequest()
{
	if (RenderAsset)
	{
		RenderAsset->CancelPendingMipChangeRequest();
		UpdateStreamingStatus(false);
	}
}

void FStreamingRenderAsset::StreamWantedMips(FRenderAssetStreamingManager& Manager)
{
	StreamWantedMips_Internal(Manager, false);
}

void FStreamingRenderAsset::CacheStreamingMetaData()
{
	bCachedForceFullyLoadHeuristic = bForceFullyLoadHeuristic;
	CachedWantedMips = WantedMips;
	CachedVisibleWantedMips = VisibleWantedMips;
}

void FStreamingRenderAsset::StreamWantedMipsUsingCachedData(FRenderAssetStreamingManager& Manager)
{
	StreamWantedMips_Internal(Manager, true);
}

void FStreamingRenderAsset::StreamWantedMips_Internal(FRenderAssetStreamingManager& Manager, bool bUseCachedData)
{
	if (RenderAsset && !RenderAsset->HasPendingUpdate())
	{
		const uint32 bLocalForceFullyLoadHeuristic = bUseCachedData ? bCachedForceFullyLoadHeuristic : bForceFullyLoadHeuristic;
		const int32 LocalVisibleWantedMips = bUseCachedData ? CachedVisibleWantedMips : VisibleWantedMips;
		// Update ResidentMips now as it is guarantied to not change here (since no pending requests).
		ResidentMips = RenderAsset->GetNumResidentMips();

		// Prevent streaming-in optional mips and non optional mips as they are from different files.
		int32 LocalWantedMips = bUseCachedData ? CachedWantedMips : WantedMips;
		if (ResidentMips < NumNonOptionalMips && LocalWantedMips > NumNonOptionalMips)
		{ 
			LocalWantedMips = NumNonOptionalMips;
		}

		if (LocalWantedMips != ResidentMips)
		{
			if (LocalWantedMips < ResidentMips)
			{
				RenderAsset->StreamOut(LocalWantedMips);
			}
			else // WantedMips > ResidentMips
			{
				const bool bShouldPrioritizeAsyncIORequest = (bLocalForceFullyLoadHeuristic || bIsTerrainTexture || bIsCharacterTexture) && LocalWantedMips <= LocalVisibleWantedMips;
				RenderAsset->StreamIn(LocalWantedMips, bShouldPrioritizeAsyncIORequest);
			}
			UpdateStreamingStatus(false);
			TrackRenderAssetEvent(this, RenderAsset, bLocalForceFullyLoadHeuristic != 0, &Manager);
		}
	}
}
