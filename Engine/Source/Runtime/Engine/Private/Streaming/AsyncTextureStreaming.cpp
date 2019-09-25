// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AsyncTextureStreaming.cpp: Definitions of classes used for texture streaming async task.
=============================================================================*/

#include "Streaming/AsyncTextureStreaming.h"
#include "Misc/App.h"
#include "Streaming/StreamingManagerTexture.h"
#include "Engine/World.h"

void FAsyncRenderAssetStreamingData::Init(
	TArray<FStreamingViewInfo> InViewInfos,
	float InLastUpdateTime,
	TArray<FLevelRenderAssetManager*>& LevelStaticInstanceManagers,
	FDynamicRenderAssetInstanceManager& DynamicComponentManager)
{
	ViewInfos = InViewInfos;
	LastUpdateTime = InLastUpdateTime;

	DynamicInstancesView = DynamicComponentManager.GetAsyncView(true);

	StaticInstancesViews.Reset();
	StaticInstancesViewIndices.Reset();
	CulledStaticInstancesViewIndices.Reset();
	StaticInstancesViewLevelIndices.Reset();

	for (int32 LevelIndex = 0; LevelIndex<LevelStaticInstanceManagers.Num(); ++LevelIndex)
	{
		if (LevelStaticInstanceManagers[LevelIndex] == nullptr)
		{
			StaticInstancesViewLevelIndices.Add(INDEX_NONE);
			continue;
		}

		const FLevelRenderAssetManager& LevelManager = *LevelStaticInstanceManagers[LevelIndex];

		if (LevelManager.IsInitialized() && LevelManager.GetLevel()->bIsVisible && LevelManager.HasRenderAssetReferences())
		{
			StaticInstancesViewLevelIndices.Add(StaticInstancesViews.Num());
			StaticInstancesViews.Add(LevelStaticInstanceManagers[LevelIndex]->GetAsyncView());
		}
		else
		{
			StaticInstancesViewLevelIndices.Add(INDEX_NONE);
		}
	}
}

void FAsyncRenderAssetStreamingData::ComputeViewInfoExtras(const FRenderAssetStreamingSettings& Settings)
{
	const float OneOverMaxHiddenPrimitiveViewBoost = 1.f / Settings.MaxHiddenPrimitiveViewBoost;
	const int32 NumViews = ViewInfos.Num();
	ViewInfoExtras.Empty(NumViews);
	ViewInfoExtras.AddUninitialized(NumViews);
	MaxScreenSizeOverAllViews = 0.f;
	
	for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
	{
		const FStreamingViewInfo& ViewInfo = ViewInfos[ViewIndex];
		FStreamingViewInfoExtra& ViewInfoExtra = ViewInfoExtras[ViewIndex];

		const float EffectiveScreenSize = (Settings.MaxEffectiveScreenSize > 0.0f) ? FMath::Min(Settings.MaxEffectiveScreenSize, ViewInfo.ScreenSize) : ViewInfo.ScreenSize;
		MaxScreenSizeOverAllViews = FMath::Max(MaxScreenSizeOverAllViews, EffectiveScreenSize);
		ViewInfoExtra.ScreenSizeFloat = EffectiveScreenSize * .5f; // Multiply by half since the ratio factors map to half the screen only
		ViewInfoExtra.ExtraBoostForVisiblePrimitiveFloat = 1.f;

		if (ViewInfo.BoostFactor > Settings.MaxHiddenPrimitiveViewBoost)
		{
			ViewInfoExtra.ScreenSizeFloat *= Settings.MaxHiddenPrimitiveViewBoost;
			ViewInfoExtra.ExtraBoostForVisiblePrimitiveFloat = ViewInfo.BoostFactor * OneOverMaxHiddenPrimitiveViewBoost;
		}
		else
		{
			ViewInfoExtra.ScreenSizeFloat *= ViewInfo.BoostFactor;
		}
	}
}

void FAsyncRenderAssetStreamingData::UpdateBoundSizes_Async(const FRenderAssetStreamingSettings& Settings)
{
	for (int32 StaticViewIndex = 0; StaticViewIndex < StaticInstancesViews.Num(); ++StaticViewIndex)
	{
		FRenderAssetInstanceAsyncView& StaticInstancesView = StaticInstancesViews[StaticViewIndex];
		StaticInstancesView.UpdateBoundSizes_Async(ViewInfos, ViewInfoExtras, LastUpdateTime, Settings);

		// Skip levels that can not contribute to resolution.
		if (StaticInstancesView.GetMaxLevelRenderAssetScreenSize() > Settings.MinLevelRenderAssetScreenSize
			|| StaticInstancesView.HasAnyComponentWithForcedLOD())
		{
			StaticInstancesViewIndices.Add(StaticViewIndex);
		}
		else
		{
			CulledStaticInstancesViewIndices.Add(StaticViewIndex);
		}
	}
	
	// Sort by max possible size, this allows early exit when iteration on many levels.
	if (Settings.MinLevelRenderAssetScreenSize > 0)
	{
		StaticInstancesViewIndices.Sort([&](int32 LHS, int32 RHS) { return StaticInstancesViews[LHS].GetMaxLevelRenderAssetScreenSize() > StaticInstancesViews[RHS].GetMaxLevelRenderAssetScreenSize(); });
	}

	DynamicInstancesView.UpdateBoundSizes_Async(ViewInfos, ViewInfoExtras, LastUpdateTime, Settings);
}

void FAsyncRenderAssetStreamingData::UpdatePerfectWantedMips_Async(FStreamingRenderAsset& StreamingRenderAsset, const FRenderAssetStreamingSettings& Settings, bool bOutputToLog) const
{
#if UE_BUILD_SHIPPING
	bOutputToLog = false;
#endif

	// Cache RenderAsset on the stack as it could be nullified on the gamethread.
	const UStreamableRenderAsset* RenderAsset = StreamingRenderAsset.RenderAsset;
	if (!RenderAsset) return;

	float MaxSize = 0;
	float MaxSize_VisibleOnly = 0;
	int32 MaxNumForcedLODs = 0;
	bool bLooksLowRes = false;

	const float MaxAllowedSize = StreamingRenderAsset.GetMaxAllowedSize(MaxScreenSizeOverAllViews);

#if !UE_BUILD_SHIPPING
	if (Settings.bStressTest)
	{
		// In stress test, we choose between the allowed mips. Combined with "r.Streaming.DropMips=2" this can also generate cancel requests.
		const int32 NumMips = FMath::RandRange(StreamingRenderAsset.MinAllowedMips, StreamingRenderAsset.MaxAllowedMips);
		MaxSize_VisibleOnly = MaxSize = StreamingRenderAsset.GetLODScreenSize(NumMips, MaxScreenSizeOverAllViews);
	}
	else
#endif
	if (Settings.bFullyLoadUsedTextures)
	{
		if (StreamingRenderAsset.LastRenderTime < 300)
		{
			MaxSize_VisibleOnly = FLT_MAX;
		}
	}
	else if (StreamingRenderAsset.MinAllowedMips == StreamingRenderAsset.MaxAllowedMips)
	{
		MaxSize_VisibleOnly = MaxSize = MaxAllowedSize;
	}
	else
	{
		const typename FStreamingRenderAsset::EAssetType AssetType = StreamingRenderAsset.RenderAssetType;
		DynamicInstancesView.GetRenderAssetScreenSize(AssetType, RenderAsset, MaxSize, MaxSize_VisibleOnly, MaxNumForcedLODs, bOutputToLog ? TEXT("Dynamic") : nullptr);

		bool bCulled = false;
		if (Settings.bMipCalculationEnablePerLevelList)
		{
			int32 LevelToIterateCount = FMath::Min(StreamingRenderAsset.LevelIndexUsage.Num(), StaticInstancesViewLevelIndices.Num());
			
			for (TBitArray<>::FIterator LevelUsageIterator(StreamingRenderAsset.LevelIndexUsage); LevelUsageIterator.GetIndex() < LevelToIterateCount; ++LevelUsageIterator)
			{
				if (!LevelUsageIterator.GetValue())
				{
					continue;
				}

				int32 ViewIndex = StaticInstancesViewLevelIndices[LevelUsageIterator.GetIndex()];

				if (ViewIndex==INDEX_NONE)
				{
					continue;
				}

				const FRenderAssetInstanceAsyncView& StaticInstancesView = StaticInstancesViews[ViewIndex];

				if (!StaticInstancesView.HasRenderAssetReferences(RenderAsset))
				{
					// Level entry has been replaced by another level
					// Remove its reference.
					StreamingRenderAsset.LevelIndexUsage[LevelUsageIterator.GetIndex()] = false;
					continue;
				}

				if (StaticInstancesView.GetMaxLevelRenderAssetScreenSize() < Settings.MinLevelRenderAssetScreenSize
					&& !StaticInstancesView.HasComponentWithForcedLOD(RenderAsset))
				{
					bCulled = true;
					continue;
				}

				// No need to iterate more if render asset is already at maximum resolution.
				if (MaxSize_VisibleOnly >= MAX_TEXTURE_SIZE || MaxNumForcedLODs >= StreamingRenderAsset.MaxAllowedMips)
				{
					break;
				}

				float TmpMaxSize = MaxSize;
				float TmpMaxVisibleOnly = MaxSize_VisibleOnly;
				int32 TmpMaxNumForcedLODs = MaxNumForcedLODs;
				
				StaticInstancesView.GetRenderAssetScreenSize(AssetType, RenderAsset, TmpMaxSize, TmpMaxVisibleOnly, TmpMaxNumForcedLODs, bOutputToLog ? TEXT("Static") : nullptr);

				MaxSize = FMath::Max(TmpMaxSize, MaxSize);
				MaxSize_VisibleOnly = FMath::Max(TmpMaxVisibleOnly, MaxSize_VisibleOnly);
				MaxNumForcedLODs = FMath::Max(TmpMaxNumForcedLODs, MaxNumForcedLODs);
			}
		}
		else
		{
			for (int32 StaticViewIndex : StaticInstancesViewIndices)
			{
				const FRenderAssetInstanceAsyncView& StaticInstancesView = StaticInstancesViews[StaticViewIndex];

				// No need to iterate more if texture is already at maximum resolution.
				if ((MaxNumForcedLODs >= StreamingRenderAsset.MaxAllowedMips
					|| MaxSize_VisibleOnly >= MAX_TEXTURE_SIZE
					|| (MaxSize_VisibleOnly > StaticInstancesView.GetMaxLevelRenderAssetScreenSize() && Settings.MinLevelRenderAssetScreenSize > 0))
					&& !bOutputToLog)
				{
					break;
				}

				StaticInstancesView.GetRenderAssetScreenSize(AssetType, RenderAsset, MaxSize, MaxSize_VisibleOnly, MaxNumForcedLODs, bOutputToLog ? TEXT("Static") : nullptr);
			}
		}

		// Don't apply to FLT_MAX since it is used as forced streaming. BoostFactor as only meaning for texture/mesh instances since the other heuristics are based on max resolution.
		if (MaxNumForcedLODs < StreamingRenderAsset.MaxAllowedMips
			&& (MaxSize > 0 || MaxSize_VisibleOnly > 0)
			&& MaxSize != FLT_MAX
			&& MaxSize_VisibleOnly != FLT_MAX)
		{
			const float CumBoostFactor = StreamingRenderAsset.BoostFactor * StreamingRenderAsset.DynamicBoostFactor;

			// If there is not enough resolution in the texture to fix the required quality, save this information to prevent degrading this texture before other ones.
			bLooksLowRes = FMath::Max3<int32>(MaxSize_VisibleOnly, MaxSize, MaxAllowedSize) / MaxAllowedSize >= CumBoostFactor * 2.f;

			MaxSize *=  CumBoostFactor;
			MaxSize_VisibleOnly *= CumBoostFactor;
		}

		// Last part checks that it has been used since the last reference was removed.
		const float TimeSinceRemoved = (float)(FApp::GetCurrentTime() - StreamingRenderAsset.InstanceRemovedTimestamp);
		StreamingRenderAsset.bUseUnkownRefHeuristic = MaxSize == 0 && MaxSize_VisibleOnly == 0 && !MaxNumForcedLODs && StreamingRenderAsset.LastRenderTime < TimeSinceRemoved - 5.f;
		if (StreamingRenderAsset.bUseUnkownRefHeuristic)
		{
			if (Settings.bMipCalculationEnablePerLevelList)
			{
				StreamingRenderAsset.bUseUnkownRefHeuristic = !bCulled;
			}
			else
			{
				// Check that it's not simply culled
				for (int32 StaticViewIndex : CulledStaticInstancesViewIndices)
				{
					const FRenderAssetInstanceAsyncView& StaticInstancesView = StaticInstancesViews[StaticViewIndex];
					if (StaticInstancesView.HasRenderAssetReferences(RenderAsset))
					{
						StreamingRenderAsset.bUseUnkownRefHeuristic = false;
						break;
					}
				}
			}

			// Ignore bUseUnkownRefHeuristic if they haven't been used in the last 90 sec.
			// If critical, it must be implemented using the ForceFullyLoad logic.
			if (StreamingRenderAsset.bUseUnkownRefHeuristic && StreamingRenderAsset.LastRenderTime < 90.0f)
			{
				if (bOutputToLog) UE_LOG(LogContentStreaming, Log,  TEXT("  UnkownRef"));
				MaxSize = FMath::Max<int32>(MaxSize, MaxAllowedSize); // affected by HiddenPrimitiveScale
				if (StreamingRenderAsset.LastRenderTime < 5.0f)
				{
					MaxSize_VisibleOnly = FMath::Max<int32>(MaxSize_VisibleOnly, MaxAllowedSize);
				}
			}
		}

		// TODO: for meshes, how to determine whether they are HLOD?
		if (StreamingRenderAsset.bForceFullyLoad
			|| (AssetType == FStreamingRenderAsset::AT_Texture
				&& StreamingRenderAsset.LODGroup == TEXTUREGROUP_HierarchicalLOD
				&& Settings.HLODStrategy == 2))
		{
			if (bOutputToLog) UE_LOG(LogContentStreaming, Log,  TEXT("  Forced FullyLoad"));
			MaxSize = FLT_MAX; // Forced load ensure the texture gets fully loaded but after what is visible/required by the other logic.
		}
		else if (AssetType == FStreamingRenderAsset::AT_Texture
			&& StreamingRenderAsset.LODGroup == TEXTUREGROUP_HierarchicalLOD
			&& Settings.HLODStrategy == 1)
		{
			if (bOutputToLog) UE_LOG(LogContentStreaming, Log,  TEXT("  HLOD Strategy"));

			if (Settings.bUseNewMetrics)
			{
				MaxSize = FMath::Max<int32>(MaxSize, MaxAllowedSize); // Affected by HiddenPrimitiveScale
			}
			else
			{
				MaxSize = FMath::Max<int32>(MaxSize, MaxAllowedSize * .5f);
			}
		}
	}

	// Previous metrics didn't handle visibility at all.
	if (!Settings.bUseNewMetrics)
	{
		MaxSize_VisibleOnly = MaxSize = FMath::Max<float>(MaxSize_VisibleOnly, MaxSize);
	}

	StreamingRenderAsset.SetPerfectWantedMips_Async(MaxSize, MaxSize_VisibleOnly, MaxScreenSizeOverAllViews, MaxNumForcedLODs, bLooksLowRes, Settings);
}

bool FRenderAssetStreamingMipCalcTask::AllowPerRenderAssetMipBiasChanges() const
{
	const TArray<FStreamingViewInfo>& ViewInfos = StreamingData.GetViewInfos();
	for (int32 ViewIndex = 0; ViewIndex < ViewInfos.Num(); ++ViewIndex)
	{
		const FStreamingViewInfo& ViewInfo = ViewInfos[ViewIndex];
		if (ViewInfo.BoostFactor > StreamingManager.Settings.PerTextureBiasViewBoostThreshold)
		{
			return false;
		}
	}
	return true;
}

void FRenderAssetStreamingMipCalcTask::UpdateBudgetedMips_Async(int64& MemoryUsed, int64& TempMemoryUsed)
{
	//*************************************
	// Update Budget
	//*************************************

	TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;
	const FRenderAssetStreamingSettings& Settings = StreamingManager.Settings;

	TArray<int32> PrioritizedRenderAssets;

	int64 MemoryBudgeted = 0;
	int64 MemoryUsedByNonTextures = 0;
	MemoryUsed = 0;
	TempMemoryUsed = 0;

	for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
	{
		if (IsAborted()) break;

		MemoryBudgeted += StreamingRenderAsset.UpdateRetentionPriority_Async(Settings.bPrioritizeMeshLODRetention);
		const int32 AssetMemUsed = StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);
		MemoryUsed += AssetMemUsed;

		// TODO: Use RHI metrics
		if (!StreamingRenderAsset.IsTexture())
		{
			MemoryUsedByNonTextures += AssetMemUsed;
		}

		if (StreamingRenderAsset.ResidentMips != StreamingRenderAsset.RequestedMips)
		{
			TempMemoryUsed += StreamingRenderAsset.GetSize(StreamingRenderAsset.RequestedMips);
		}
	}

	//*************************************
	// Update Effective Budget
	//*************************************

	bool bResetMipBias = false;

	if (PerfectWantedMipsBudgetResetThresold - MemoryBudgeted > TempMemoryBudget + MemoryMargin)
	{
		// Reset the budget tradeoffs if the required pool size shrinked significantly.
		PerfectWantedMipsBudgetResetThresold = MemoryBudgeted;
		bResetMipBias = true;
	}
	else if (MemoryBudgeted > PerfectWantedMipsBudgetResetThresold)
	{
		// Keep increasing the threshold since higher requirements incurs bigger tradeoffs.
		PerfectWantedMipsBudgetResetThresold = MemoryBudgeted; 
	}


	const int64 NonStreamingRenderAssetMemory =  AllocatedMemory - MemoryUsed + MemoryUsedByNonTextures;
	int64 AvailableMemoryForStreaming = PoolSize - NonStreamingRenderAssetMemory - MemoryMargin;

	// If the platform defines a max VRAM usage, check if the pool size must be reduced,
	// but also check if it would be safe to some of the NonStreamingRenderAssetMemory from the pool size computation.
	// The later helps significantly in low budget settings, where NonStreamingRenderAssetMemory would take too much of the pool.
	if (GPoolSizeVRAMPercentage > 0 && TotalGraphicsMemory > 0)
	{
		const int64 UsableVRAM = FMath::Max<int64>(TotalGraphicsMemory * GPoolSizeVRAMPercentage / 100, TotalGraphicsMemory - Settings.VRAMPercentageClamp * 1024ll * 1024ll);
		const int64 UsedVRAM = (int64)GCurrentRendertargetMemorySize * 1024ll + NonStreamingRenderAssetMemory; // Add any other...
		const int64 AvailableVRAMForStreaming = FMath::Min<int64>(UsableVRAM - UsedVRAM - MemoryMargin, PoolSize);
		if (Settings.bLimitPoolSizeToVRAM || AvailableVRAMForStreaming > AvailableMemoryForStreaming)
		{
			AvailableMemoryForStreaming = AvailableVRAMForStreaming;
		}
	}

	// Update EffectiveStreamingPoolSize, trying to stabilize it independently of temp memory, allocator overhead and non-streaming resources normal variation.
	// It's hard to know how much temp memory and allocator overhead is actually in AllocatedMemorySize as it is platform specific.
	// We handle it by not using all memory available. If temp memory and memory margin values are effectively bigger than the actual used values, the pool will stabilize.
	if (AvailableMemoryForStreaming < MemoryBudget)
	{
		// Reduce size immediately to avoid taking more memory.
		MemoryBudget = FMath::Max<int64>(AvailableMemoryForStreaming, 0);
	}
	else if (AvailableMemoryForStreaming - MemoryBudget > TempMemoryBudget + MemoryMargin)
	{
		// Increase size considering that the variation does not come from temp memory or allocator overhead (or other recurring cause).
		// It's unclear how much temp memory is actually in there, but the value will decrease if temp memory increases.
		MemoryBudget = AvailableMemoryForStreaming;
		bResetMipBias = true;
	}

	//*******************************************
	// Reset per mip bias if not required anymore.
	//*******************************************

	// When using mip per texture/mesh, the BudgetMipBias gets reset when the required resolution does not get affected anymore by the BudgetMipBias.
	// This allows texture/mesh to reset their bias when the viewpoint gets far enough, or the primitive is not visible anymore.
	if (Settings.bUsePerTextureBias)
	{
		for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
		{
			if (IsAborted()) break;

			if (StreamingRenderAsset.BudgetMipBias > 0
				&& (bResetMipBias
					|| FMath::Max<int32>(
						StreamingRenderAsset.VisibleWantedMips,
						StreamingRenderAsset.HiddenWantedMips + StreamingRenderAsset.NumMissingMips) < StreamingRenderAsset.MaxAllowedMips))
			{
				StreamingRenderAsset.BudgetMipBias = 0;
			}
		}
	}

	//*************************************
	// Drop Mips
	//*************************************

	// If the budget is taking too much, drop some mips.
	if (MemoryBudgeted > MemoryBudget && !IsAborted())
	{
		//*************************************
		// Get texture/mesh list in order of reduction
		//*************************************

		PrioritizedRenderAssets.Empty(StreamingRenderAssets.Num());

		for (int32 AssetIndex = 0; AssetIndex < StreamingRenderAssets.Num() && !IsAborted(); ++AssetIndex)
		{
			FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
			// Only consider non deleted textures/meshes (can change any time).
			if (!StreamingRenderAsset.RenderAsset) continue;

			// Ignore textures/meshes for which we are not allowed to reduce resolution.
			if (!StreamingRenderAsset.IsMaxResolutionAffectedByGlobalBias()) continue;

			// Ignore texture/mesh that can't drop any mips
			const int32 MinAllowedMips = FMath::Max(StreamingRenderAsset.MinAllowedMips, StreamingRenderAsset.NumForcedMips);
			if (StreamingRenderAsset.BudgetedMips > MinAllowedMips)
			{
				PrioritizedRenderAssets.Add(AssetIndex);
			}
		}

		// Sort texture/mesh, having those that should be dropped first.
		PrioritizedRenderAssets.Sort(FCompareRenderAssetByRetentionPriority(StreamingRenderAssets));


		if (Settings.bUsePerTextureBias && AllowPerRenderAssetMipBiasChanges())
		{
			//*************************************
			// Drop Max Resolution until in budget.
			//*************************************

			// When using mip bias per texture/mesh, we first reduce the maximum resolutions (if used) in order to fit.
			for (int32 NumDroppedMips = 0; NumDroppedMips < Settings.GlobalMipBias && MemoryBudgeted > MemoryBudget && !IsAborted(); ++NumDroppedMips)
			{
				const int64 PreviousMemoryBudgeted = MemoryBudgeted;

				// Heuristic: Only consider dropping max resolution for a mesh if it has reasonable impact on memory reduction.
				// Currently, reasonable impact is defined as MemDeltaOfDroppingOneLOD >= MinTextureMemDelta in this pass.
				int64 MinTextureMemDelta = MAX_int64;

				for (int32 PriorityIndex = PrioritizedRenderAssets.Num() - 1; PriorityIndex >= 0 && MemoryBudgeted > MemoryBudget && !IsAborted(); --PriorityIndex)
				{
					int32 AssetIndex = PrioritizedRenderAssets[PriorityIndex];
					if (AssetIndex == INDEX_NONE) continue;

					FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
					const int32 MinAllowedMips = FMath::Max(StreamingRenderAsset.MinAllowedMips, StreamingRenderAsset.NumForcedMips);
					if (StreamingRenderAsset.BudgetedMips <= MinAllowedMips)
					{
						// Don't try this one again.
						PrioritizedRenderAssets[PriorityIndex] = INDEX_NONE;
						continue;
					}

					// If the texture/mesh requires a high resolution mip, consider dropping it. 
					// When considering dropping the first mip, only textures/meshes using the first mip will drop their resolution, 
					// But when considering dropping the second mip, textures/meshes using their first and second mips will loose it.
					if (StreamingRenderAsset.MaxAllowedMips + StreamingRenderAsset.BudgetMipBias - NumDroppedMips <= StreamingRenderAsset.BudgetedMips)
					{
						const int32 NumMipsToDrop = NumDroppedMips + 1 - StreamingRenderAsset.BudgetMipBias;

						if (Settings.bPrioritizeMeshLODRetention)
						{
							const bool bIsTexture = StreamingRenderAsset.IsTexture();
							const int64 MemDeltaFromMaxResDrop = StreamingRenderAsset.GetDropMaxResMemDelta(NumMipsToDrop);

							if (!MemDeltaFromMaxResDrop || (!bIsTexture && MemDeltaFromMaxResDrop < MinTextureMemDelta && MinTextureMemDelta != MAX_int64))
							{
								continue;
							}

							MinTextureMemDelta = bIsTexture ? FMath::Min(MinTextureMemDelta, MemDeltaFromMaxResDrop) : MinTextureMemDelta;
						}

						MemoryBudgeted -= StreamingRenderAsset.DropMaxResolution_Async(NumMipsToDrop);
					}
				}

				// Break when memory does not change anymore
				if (PreviousMemoryBudgeted == MemoryBudgeted)
				{
					break;
				}
			}
		}

		//*************************************
		// Drop WantedMip until in budget.
		//*************************************

		while (MemoryBudgeted > MemoryBudget && !IsAborted())
		{
			const int64 PreviousMemoryBudgeted = MemoryBudgeted;

			// Heuristic: only start considering dropping mesh LODs if it has reasonable impact on memory reduction.
			int64 MinTextureMemDelta = MAX_int64;

			// Drop from the lowest priority first (starting with last elements)
			for (int32 PriorityIndex = PrioritizedRenderAssets.Num() - 1; PriorityIndex >= 0 && MemoryBudgeted > MemoryBudget && !IsAborted(); --PriorityIndex)
			{
				int32 AssetIndex = PrioritizedRenderAssets[PriorityIndex];
				if (AssetIndex == INDEX_NONE) continue;

				FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
				const int32 MinAllowedMips = FMath::Max(StreamingRenderAsset.MinAllowedMips, StreamingRenderAsset.NumForcedMips);
				if (StreamingRenderAsset.BudgetedMips <= MinAllowedMips)
				{
					// Don't try this one again.
					PrioritizedRenderAssets[PriorityIndex] = INDEX_NONE;
					continue;
				}

				const bool bIsTexture = StreamingRenderAsset.IsTexture();
				const bool bIsMesh = !bIsTexture;
				if (Settings.bPrioritizeMeshLODRetention && bIsMesh)
				{
					const int64 PredictedMemDelta = StreamingRenderAsset.GetDropOneMipMemDelta();
					if (PredictedMemDelta < MinTextureMemDelta && MinTextureMemDelta != MAX_int64)
					{
						continue;
					}
				}

				// If this texture/mesh has already missing mips for its normal quality, don't drop more than required..
				if (StreamingRenderAsset.NumMissingMips > 0)
				{
					--StreamingRenderAsset.NumMissingMips;
					continue;
				}

				const int64 MemDelta = StreamingRenderAsset.DropOneMip_Async();
				MemoryBudgeted -= MemDelta;
				if (Settings.bPrioritizeMeshLODRetention && bIsTexture && MemDelta > 0)
				{
					MinTextureMemDelta = FMath::Min(MinTextureMemDelta, MemDelta);
				}
			}

			// Break when memory does not change anymore
			if (PreviousMemoryBudgeted == MemoryBudgeted)
			{
				break;
			}
		}
	}

	//*************************************
	// Keep Mips
	//*************************************

	// If there is some room left, try to keep as much as long as it won't bust budget.
	// This will run even after sacrificing to fit in budget since some small unwanted mips could still be kept.
	if (MemoryBudgeted < MemoryBudget && !IsAborted())
	{
		PrioritizedRenderAssets.Empty(StreamingRenderAssets.Num());
		const int64 MaxMipSize = MemoryBudget - MemoryBudgeted;
		for (int32 AssetIndex = 0; AssetIndex < StreamingRenderAssets.Num() && !IsAborted(); ++AssetIndex)
		{
			FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
			// Only consider non deleted textures/meshes (can change any time).
			if (!StreamingRenderAsset.RenderAsset) continue;

			// Only consider textures/meshes that won't bust budget nor generate new I/O requests
			if (StreamingRenderAsset.BudgetedMips < StreamingRenderAsset.ResidentMips &&
				StreamingRenderAsset.GetSize(StreamingRenderAsset.BudgetedMips + 1) - StreamingRenderAsset.GetSize(StreamingRenderAsset.BudgetedMips) <= MaxMipSize)
			{
				PrioritizedRenderAssets.Add(AssetIndex);
			}
		}

		// Sort texture/mesh, having those that should be dropped first.
		PrioritizedRenderAssets.Sort(FCompareRenderAssetByRetentionPriority(StreamingRenderAssets));

		bool bBudgetIsChanging = true;
		while (MemoryBudgeted < MemoryBudget && bBudgetIsChanging && !IsAborted())
		{
			bBudgetIsChanging = false;

			// Keep from highest priority first.
			for (int32 PriorityIndex = 0; PriorityIndex < PrioritizedRenderAssets.Num() && MemoryBudgeted < MemoryBudget && !IsAborted(); ++PriorityIndex)
			{
				int32 AssetIndex = PrioritizedRenderAssets[PriorityIndex];
				if (AssetIndex == INDEX_NONE) continue;

				FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
				int64 TakenMemory = StreamingRenderAsset.KeepOneMip_Async();

				if (TakenMemory > 0)
				{
					if (MemoryBudgeted + TakenMemory <= MemoryBudget)
					{
						MemoryBudgeted += TakenMemory;
						bBudgetIsChanging = true;
					}
					else // Cancel keeping this mip
					{
						StreamingRenderAsset.DropOneMip_Async();
						PrioritizedRenderAssets[PriorityIndex] = INDEX_NONE; // Don't try this one again.
					}
				}
				else // No other mips to keep.
				{
					PrioritizedRenderAssets[PriorityIndex] = INDEX_NONE; // Don't try this one again.
				}
			}
		}
	}

	//*************************************
	// Handle drop mips debug option
	//*************************************
#if !UE_BUILD_SHIPPING
	if (Settings.DropMips > 0)
	{
		for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
		{
			if (IsAborted()) break;

			if (Settings.DropMips == 1)
			{
				StreamingRenderAsset.BudgetedMips = FMath::Min<int32>(StreamingRenderAsset.BudgetedMips, StreamingRenderAsset.GetPerfectWantedMips());
			}
			else
			{
				StreamingRenderAsset.BudgetedMips = FMath::Min<int32>(StreamingRenderAsset.BudgetedMips, StreamingRenderAsset.VisibleWantedMips);
			}
		}
	}
#endif
}

void FRenderAssetStreamingMipCalcTask::UpdateLoadAndCancelationRequests_Async(int64 MemoryUsed, int64 TempMemoryUsed)
{
	TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;
	const FRenderAssetStreamingSettings& Settings = StreamingManager.Settings;

	TArray<int32> PrioritizedRenderAssets;
	PrioritizedRenderAssets.Empty(StreamingRenderAssets.Num());
	for (int32 AssetIndex = 0; AssetIndex < StreamingRenderAssets.Num() && !IsAborted(); ++AssetIndex)
	{
		FStreamingRenderAsset& StreamingTexture = StreamingRenderAssets[AssetIndex];
		if (StreamingTexture.UpdateLoadOrderPriority_Async(Settings.MinMipForSplitRequest))
		{
			PrioritizedRenderAssets.Add(AssetIndex);
		}
	}
	PrioritizedRenderAssets.Sort(FCompareRenderAssetByLoadOrderPriority(StreamingRenderAssets));

	LoadRequests.Empty();
	CancelationRequests.Empty();

	// Now fill in the LoadRequest and CancelationRequests
	for (int32 PriorityIndex = 0; PriorityIndex < PrioritizedRenderAssets.Num() && !IsAborted(); ++PriorityIndex)
	{
		int32 AssetIndex = PrioritizedRenderAssets[PriorityIndex];
		FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];

		// If there is a pending update with no cancelation request
		if (StreamingRenderAsset.bInFlight && StreamingRenderAsset.RequestedMips != StreamingRenderAsset.ResidentMips)
		{
			// If there is a pending load that attempts to load unrequired data (by at least 2 mips), 
			// or if there is a pending unload that attempts to unload required data, try to cancel it.
			if (StreamingRenderAsset.RequestedMips > FMath::Max<int32>(StreamingRenderAsset.ResidentMips, StreamingRenderAsset.WantedMips + 1 ) ||
				StreamingRenderAsset.RequestedMips < FMath::Min<int32>(StreamingRenderAsset.ResidentMips, StreamingRenderAsset.WantedMips ))
			{
				CancelationRequests.Add(AssetIndex);
			}
		}
		else if (StreamingRenderAsset.WantedMips < StreamingRenderAsset.ResidentMips && TempMemoryUsed < TempMemoryBudget)
		{
			const int64 TempMemoryRequired = StreamingRenderAsset.GetSize(StreamingRenderAsset.WantedMips);
			const int64 UsedMemoryRequired = StreamingRenderAsset.GetSize(StreamingRenderAsset.WantedMips) - StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);

			// Respect the temporary budget unless this is the first unload request. This allows a single mip update of any size.
			if (TempMemoryUsed + TempMemoryRequired <= TempMemoryBudget || !LoadRequests.Num())
			{
				LoadRequests.Add(AssetIndex);
	
				MemoryUsed -= UsedMemoryRequired;
				TempMemoryUsed += TempMemoryRequired;
			}
		}
		else if (StreamingRenderAsset.WantedMips > StreamingRenderAsset.ResidentMips && TempMemoryUsed < TempMemoryBudget)
		{
			const int64 UsedMemoryRequired = StreamingRenderAsset.GetSize(StreamingRenderAsset.WantedMips) - StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);
			const int64 TempMemoryRequired = StreamingRenderAsset.GetSize(StreamingRenderAsset.WantedMips);

			// Respect the temporary budget unless this is the first load request. This allows a single mip update of any size.
			if (TempMemoryUsed + TempMemoryRequired <= TempMemoryBudget || !LoadRequests.Num())
			{
				LoadRequests.Add(AssetIndex);
	
				MemoryUsed += UsedMemoryRequired;
				TempMemoryUsed += TempMemoryRequired;
			}
		}
	}
}

void FRenderAssetStreamingMipCalcTask::UpdatePendingStreamingStatus_Async()
{
	TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;
	const bool bIsStreamingPaused = StreamingManager.bPauseRenderAssetStreaming;

	PendingUpdateDirties.Empty();

	for (int32 AssetIndex = 0; AssetIndex < StreamingRenderAssets.Num() && !IsAborted(); ++AssetIndex)
	{
		const FStreamingRenderAsset& StreamingTexture = StreamingRenderAssets[AssetIndex];
		if (StreamingTexture.bHasUpdatePending != StreamingTexture.HasUpdatePending(bIsStreamingPaused, HasAnyView()))
		{
			// The texture/mesh state are only updated on the gamethread, where we can make sure the UStreamableRenderAsset is in sync.
			PendingUpdateDirties.Add(AssetIndex);
		}
	}
}

void FRenderAssetStreamingMipCalcTask::DoWork()
{
	SCOPED_NAMED_EVENT(FRenderAssetStreamingMipCalcTask_DoWork, FColor::Turquoise);
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FRenderAssetStreamingMipCalcTask::DoWork"), STAT_FRenderAssetStreamingMipCalcTask_DoWork, STATGROUP_StreamingDetails);

	// While the async task is runnning, the StreamingRenderAssets are guarantied not to be reallocated.
	// 2 things can happen : a texture can be removed, in which case the texture will be set to null
	// or some members can be updated following calls to UpdateDynamicData().
	TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;
	const FRenderAssetStreamingSettings& Settings = StreamingManager.Settings;

	StreamingData.ComputeViewInfoExtras(Settings);

	// Update the distance and size for each bounds.
	StreamingData.UpdateBoundSizes_Async(Settings);
	
	if (StreamingManager.GetAndResetNewFilesHaveLoaded())
	{
		for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
		{
			if (IsAborted()) break;
			StreamingRenderAsset.ClearCachedOptionalMipsState_Async();
		}
	}

	for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
	{
		if (IsAborted()) break;

		StreamingRenderAsset.UpdateOptionalMipsState_Async();
		
		StreamingData.UpdatePerfectWantedMips_Async(StreamingRenderAsset, Settings);
		StreamingRenderAsset.DynamicBoostFactor = 1.f; // Reset after every computation.
	}

	int64 MemoryUsed, TempMemoryUsed;
	// According to budget, make relevant sacrifices and keep possible unwanted mips
	UpdateBudgetedMips_Async(MemoryUsed, TempMemoryUsed);

	// Update load requests.
	UpdateLoadAndCancelationRequests_Async(MemoryUsed, TempMemoryUsed);

	// Update bHasStreamingUpdatePending
	UpdatePendingStreamingStatus_Async();

	StreamingData.OnTaskDone_Async();

#if STATS
	UpdateStats_Async();
#elif UE_BUILD_TEST
	UpdateCSVOnlyStats_Async();
#endif // STATS
}

void FRenderAssetStreamingMipCalcTask::UpdateStats_Async()
{
#if STATS
	FRenderAssetStreamingStats& Stats = StreamingManager.GatheredStats;
	FRenderAssetStreamingSettings& Settings = StreamingManager.Settings;
	TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;

	Stats.RenderAssetPool = PoolSize;
	// Stats.StreamingPool = MemoryBudget;
	Stats.UsedStreamingPool = 0;

	Stats.SafetyPool = MemoryMargin; 
	Stats.TemporaryPool = TempMemoryBudget;
	Stats.StreamingPool = MemoryBudget;
	Stats.NonStreamingMips = AllocatedMemory;

	Stats.RequiredPool = 0;
	Stats.VisibleMips = 0;
	Stats.HiddenMips = 0;

	Stats.ForcedMips = 0;
	Stats.UnkownRefMips = 0;

	Stats.CachedMips = 0;

	Stats.WantedMips = 0;
	Stats.PendingRequests = 0;

	Stats.OverBudget = 0;

	Stats.NumStreamedMeshes = 0;
	Stats.AvgNumStreamedLODs = 0.f;
	Stats.AvgNumResidentLODs = 0.f;
	Stats.AvgNumEvictedLODs = 0.f;
	Stats.StreamedMeshMem = 0;
	Stats.ResidentMeshMem = 0;
	Stats.EvictedMeshMem = 0;
	int32 TotalNumStreamedLODs = 0;
	int32 TotalNumResidentLODs = 0;
	int32 TotalNumEvictedLODs = 0;

	for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
	{
		if (IsAborted()) break;
		if (!StreamingRenderAsset.RenderAsset) continue;

		const int64 ResidentSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);
		const int64 RequiredSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.GetPerfectWantedMips());
		const int64 BudgetedSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.BudgetedMips);
		const int64 MaxSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.MaxAllowedMips);
		const int64 VisibleWantedSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.VisibleWantedMips);

		// How much the streamer would use if there was no limit.
		Stats.RequiredPool += RequiredSize;

		// How much the streamer actually use.
		Stats.UsedStreamingPool += FMath::Min<int64>(RequiredSize, BudgetedSize);

		// Remove from the non streaming budget what is actually taken by streaming.
		Stats.NonStreamingMips -= ResidentSize;

		// All persistent mip bias bigger than the expected is considered overbudget.
		const int32 OverBudgetBias = FMath::Max<int32>(0, StreamingRenderAsset.BudgetMipBias - Settings.GlobalMipBias);
		Stats.OverBudget += StreamingRenderAsset.GetSize(StreamingRenderAsset.MaxAllowedMips + OverBudgetBias) - MaxSize;

		const int64 UsedSize = FMath::Min3<int64>(RequiredSize, BudgetedSize, ResidentSize);

		Stats.WantedMips += UsedSize;
		Stats.CachedMips += FMath::Max<int64>(ResidentSize - UsedSize, 0);

		if (GIsEditor && StreamingRenderAsset.bForceFullyLoadHeuristic)
		{
			Stats.ForcedMips += UsedSize;
		}
		else if (StreamingRenderAsset.bUseUnkownRefHeuristic)
		{
			Stats.UnkownRefMips += UsedSize;
		}
		else
		{
			if (VisibleWantedSize >= UsedSize)
			{
				Stats.VisibleMips += UsedSize;
			}
			else // VisibleWantedSize < UsedSize
			{
				Stats.VisibleMips += VisibleWantedSize;

				// Forced mips are not the same as hidden mips as they are loaded because the user wants them absolutly
				if (StreamingRenderAsset.bForceFullyLoadHeuristic
					|| (StreamingRenderAsset.IsTexture()
						&& StreamingRenderAsset.LODGroup == TEXTUREGROUP_HierarchicalLOD
						&& Settings.HLODStrategy > 0))
				{
					Stats.ForcedMips += UsedSize - VisibleWantedSize;
				}
				else
				{
					Stats.HiddenMips += UsedSize - VisibleWantedSize;
				}
			}
		}

		if (StreamingRenderAsset.RequestedMips > StreamingRenderAsset.ResidentMips)
		{
			Stats.PendingRequests += StreamingRenderAsset.GetSize(StreamingRenderAsset.RequestedMips) - ResidentSize;
		}

		if (StreamingRenderAsset.IsMesh())
		{
			const bool bOptLODsExist = StreamingRenderAsset.OptionalMipsState == FStreamingRenderAsset::OMS_HasOptionalMips;
			const int32 NumLODs = bOptLODsExist ? StreamingRenderAsset.MipCount : StreamingRenderAsset.NumNonOptionalMips;
			const int32 NumStreamedLODs = NumLODs - StreamingRenderAsset.NumNonStreamingMips;
			const int32 NumResidentLODs = StreamingRenderAsset.ResidentMips;
			const int32 NumEvictedLODs = NumLODs - NumResidentLODs;
			const int64 TotalSize = StreamingRenderAsset.GetSize(NumLODs);
			const int64 StreamedSize = TotalSize - StreamingRenderAsset.GetSize(StreamingRenderAsset.NumNonStreamingMips);
			const int64 EvictedSize = TotalSize - ResidentSize;

			++Stats.NumStreamedMeshes;
			TotalNumStreamedLODs += NumStreamedLODs;
			TotalNumResidentLODs += NumResidentLODs;
			TotalNumEvictedLODs += NumEvictedLODs;
			Stats.StreamedMeshMem += StreamedSize;
			Stats.ResidentMeshMem += ResidentSize;
			Stats.EvictedMeshMem += EvictedSize;
		}
	}

	if (Stats.NumStreamedMeshes > 0)
	{
		Stats.AvgNumStreamedLODs = (float)TotalNumStreamedLODs / Stats.NumStreamedMeshes;
		Stats.AvgNumResidentLODs = (float)TotalNumResidentLODs / Stats.NumStreamedMeshes;
		Stats.AvgNumEvictedLODs = (float)TotalNumEvictedLODs / Stats.NumStreamedMeshes;
	}

	Stats.OverBudget += FMath::Max<int64>(Stats.RequiredPool - Stats.StreamingPool, 0);
	Stats.Timestamp = FPlatformTime::Seconds();
#endif
}

void FRenderAssetStreamingMipCalcTask::UpdateCSVOnlyStats_Async()
{
	const TArray<FStreamingRenderAsset>& StreamingRenderAssets = StreamingManager.StreamingRenderAssets;

	FRenderAssetStreamingStats& Stats = StreamingManager.GatheredStats;

	Stats.RenderAssetPool = PoolSize;

	Stats.SafetyPool = MemoryMargin;
	Stats.TemporaryPool = TempMemoryBudget;
	Stats.StreamingPool = MemoryBudget;
	Stats.NonStreamingMips = AllocatedMemory;

	Stats.RequiredPool = 0;
	Stats.CachedMips = 0;
	Stats.WantedMips = 0;

	for (const FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
	{
		if (IsAborted()) break;
		if (!StreamingRenderAsset.RenderAsset) continue;

		const int64 ResidentSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);
		const int64 RequiredSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.GetPerfectWantedMips());
		const int64 BudgetedSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.BudgetedMips);

		// How much the streamer would use if there was no limit.
		Stats.RequiredPool += RequiredSize;

		// Remove from the non streaming budget what is actually taken by streaming.
		Stats.NonStreamingMips -= ResidentSize;

		const int64 UsedSize = FMath::Min3<int64>(RequiredSize, BudgetedSize, ResidentSize);

		Stats.WantedMips += UsedSize;
		Stats.CachedMips += FMath::Max<int64>(ResidentSize - UsedSize, 0);
	}
}
