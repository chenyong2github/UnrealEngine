// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
StreamingTexture.h: Definitions of classes used for texture streaming.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

struct FRenderAssetStreamingManager;
struct FRenderAssetStreamingSettings;

/*-----------------------------------------------------------------------------
	FStreamingRenderAsset, the streaming system's version of UTexture2D.
-----------------------------------------------------------------------------*/

/** Self-contained structure to manage a streaming texture/mesh, possibly on a separate thread. */
struct FStreamingRenderAsset
{
	static constexpr int32 MaxNumMeshLODs = MAX_MESH_LOD_COUNT;
	static_assert(2 * MaxNumMeshLODs >= MAX_TEXTURE_MIP_COUNT, "Failed mip count assumption");

	enum EOptionalMipsState : uint8
	{
		OMS_NotCached,
		OMS_NoOptionalMips,
		OMS_HasOptionalMips,
		OMS_Num
	};

	enum EAssetType : uint8
	{
		AT_Texture,
		AT_StaticMesh,
		AT_SkeletalMesh,
		AT_Num
	};

	FStreamingRenderAsset(
		UStreamableRenderAsset* InRenderAsset,
		const int32* NumStreamedMips,
		int32 NumLODGroups,
		EAssetType InAssetType,
		const FRenderAssetStreamingSettings& Settings);

	/** Update data that should not change unless changing settings. */
	void UpdateStaticData(const FRenderAssetStreamingSettings& Settings);

	/** Update data that the engine could change through gameplay. */
	void UpdateDynamicData(const int32* NumStreamedMips, int32 NumLODGroups, const FRenderAssetStreamingSettings& Settings, bool bWaitForMipFading);

	/** Lightweight version of UpdateDynamicData. */
	void UpdateStreamingStatus(bool bWaitForMipFading);

	/**
	 * Returns the amount of memory used by the texture/mesh given a specified number of mip-maps, in bytes.
	 *
	 * @param MipCount	Number of mip-maps to account for
	 * @return			Total amount of memory used for the specified mip-maps, in bytes
	 */
	int32 GetSize( int32 InMipCount ) const
	{
		check(InMipCount > 0);
		check(InMipCount <= (IsTexture() ? MAX_TEXTURE_MIP_COUNT : MaxNumMeshLODs));
		return CumulativeLODSizes[InMipCount - 1];
	}

	static const TCHAR* GetStreamingAssetTypeStr(EAssetType InAssetType)
	{
		switch (InAssetType)
		{
		case AT_Texture:
			return TEXT("Texture");
		case AT_StaticMesh:
			return TEXT("StaticMesh");
		case AT_SkeletalMesh:
			return TEXT("SkeletalMesh");
		default:
			return TEXT("Unkown");
		}
	}

	static float GetDefaultExtraBoost(bool bUseNewMetrics)
	{
		// When accurate distance computation, we need to relax the distance otherwise it gets too conservative. (ex 513 goes to 1024)
		return bUseNewMetrics ? .71f : 1.f;
	}

	static float GetExtraBoost(TextureGroup	LODGroup, const FRenderAssetStreamingSettings& Settings);

	int32 GetWantedMipsFromSize(float Size, float MaxScreenSizeOverAllViews) const;

	/** Set the wanted mips from the async task data */
	void SetPerfectWantedMips_Async(
		float MaxSize,
		float MaxSize_VisibleOnly,
		float MaxScreenSizeOverAllViews,
		int32 MaxNumForcedLODs,
		bool InLooksLowRes,
		const FRenderAssetStreamingSettings& Settings);

	/** Init BudgetedMip and update RetentionPriority. Returns the size that would be taken if all budgeted mips where loaded. */
	int64 UpdateRetentionPriority_Async(bool bPrioritizeMesh);

	/** Reduce the maximum allowed resolution by 1 mip. Return the size freed by doing so. */
	int64 DropMaxResolution_Async(int32 NumDroppedMips);

	/** Reduce BudgetedMip by 1 and return the size freed by doing so. */
	int64 DropOneMip_Async();

	/** Increase BudgetedMip by 1, up to resident mips, and return the size taken. */
	int64 KeepOneMip_Async();

	/** Return the memory delta in bytes caused by max resolution change. Actual memory reduction is smaller or equal. **/
	int64 GetDropMaxResMemDelta(int32 NumDroppedMips) const;

	/** Return the memory delta in bytes if a mip is successfully dropped. */
	int64 GetDropOneMipMemDelta() const;

	float GetMaxAllowedSize(float MaxScreenSizeOverAllViews) const
	{
		return IsTexture() ? (float)(0x1 << (MaxAllowedMips - 1)) : MaxScreenSizeOverAllViews;
	}

	float GetNormalizedScreenSize(int32 NumMips) const
	{
		check(IsMesh());
		check(NumMips > 0 && NumMips <= MipCount);
		return LODScreenSizes[NumMips - 1];
	}

	float GetLODScreenSize(int32 NumMips, float MaxScreenSizeOverAllViews) const
	{
		check(NumMips > 0 && NumMips <= MipCount);
		return IsTexture() ?
			static_cast<float>(1 << (NumMips - 1)) :
			GetNormalizedScreenSize(NumMips) * MaxScreenSizeOverAllViews;
	}

	/** Init load order. Return wether this texture has any load/unload request */
	bool UpdateLoadOrderPriority_Async(int32 MinMipForSplitRequest);

	void UpdateOptionalMipsState_Async();
	
	void CancelPendingMipChangeRequest();
	void StreamWantedMips(FRenderAssetStreamingManager& Manager);

	// Cache meta data (e.g. WantedMips) for StreamWantedMipsUsingCachedData to use later on
	void CacheStreamingMetaData();

	// Stream using the meta data produced by last run of FRenderAssetStreamingMipCalcTask.
	// This allows streaming to happen in parallel with the async update task
	void StreamWantedMipsUsingCachedData(FRenderAssetStreamingManager& Manager);

	bool IsTexture() const
	{
		return RenderAssetType == AT_Texture;
	}

	bool IsMesh() const
	{
		// FRenderAssetStreamingManager only handles textures and meshes currently
		return RenderAssetType != AT_Texture;
	}

	FORCEINLINE int32 GetPerfectWantedMips() const { return FMath::Max<int32>(VisibleWantedMips,  HiddenWantedMips); }

	// Whether this texture/mesh can be affected by Global Bias and Budget Bias per texture/mesh.
	// It controls if the texture/mesh resolution can be scarificed to fit into budget.
	FORCEINLINE bool IsMaxResolutionAffectedByGlobalBias() const 
	{
		// In editor, forced stream in should never have reduced mips as they can be edited.
		return (IsMesh() || LODGroup != TEXTUREGROUP_HierarchicalLOD)
			&& !bIsTerrainTexture
			&& !bIgnoreStreamingMipBias
			&& !(GIsEditor && bForceFullyLoadHeuristic); 
	}

	FORCEINLINE bool HasUpdatePending(bool bIsStreamingPaused, bool bHasViewPoint) const 
	{
		const bool bBudgetedMipsIsValid = bHasViewPoint || bForceFullyLoadHeuristic; // Force fully load don't need any viewpoint info.
		// If paused, nothing will update anytime soon.
		// If more mips will be streamed in eventually, wait.
		// Otherwise, if the distance based computation had no viewpoint, wait.
		return !bIsStreamingPaused && (BudgetedMips > ResidentMips || !bBudgetedMipsIsValid);
	}

	FORCEINLINE void ClearCachedOptionalMipsState_Async()
	{
		// If we already have our optional mips there is no need to recache, pak files can't go away!
		if (OptionalMipsState == EOptionalMipsState::OMS_NoOptionalMips && NumNonOptionalMips != MipCount)
		{
			OptionalMipsState = EOptionalMipsState::OMS_NotCached;
		}
	}

	/***************************************************************
	 * Member data categories:
	 * (1) Members initialized when this is constructed => NEVER CHANGES
	 * (2) Cached dynamic members that need constant update => UPDATES IN UpdateDynamicData()
	 * (3) Helper data set by the streamer to handle special cases => CHANGES ANYTIME (gamethread)
	 * (4) Data generated by the async task. CHANGES ANYTIME (taskthread)
	 * (5) Data cached to allow streaming to run in parallel with meta data update
	 ***************************************************************/

	/** (1) Texture/mesh to manage. Note that this becomes null when the texture/mesh is removed. */
	UStreamableRenderAsset*		RenderAsset;
	/** (2) */
	FString			OptionalBulkDataFilename;
	
	/** (1) Cached texture/mesh LOD group. */
	int32	LODGroup;
	/** (1) Cached number of mipmaps that are not allowed to stream. */
	int32			NumNonStreamingMips;
	/** (1) Cached number of mip-maps in the asset's mip array (including the base mip) */
	int32			MipCount;
	/** (1) Sum of all boost factors that applies to this texture/mesh. */
	float			BoostFactor;
	/** (1) Cached memory sizes for each possible mipcount. */
	union
	{
		int32 CumulativeLODSizes[2 * MaxNumMeshLODs];
		struct
		{
			int32 CumulativeLODSizes_Mesh[MaxNumMeshLODs];
			// Normalized size of projected bounding sphere - [0, 1]
			float LODScreenSizes[MaxNumMeshLODs];
		};
	};

	/** (2) Cached number of mip-maps in memory (including the base mip) */
	int32			ResidentMips;
	/** (2) Min number of mip-maps requested by the streaming system. */
	int32			RequestedMips;
	/** (2) Min mip to be requested by the streaming  */
	int32			MinAllowedMips;
	/** (2) Max mip to be requested by the streaming  */
	int32			MaxAllowedMips;
	/** (2) Mips which are in an optional bulk data file (may not be present on device) */
	int32			NumNonOptionalMips;
	/** (2) How much game time has elapsed since the texture was bound for rendering. Based on FApp::GetCurrentTime(). */
	float			LastRenderTime;

	/** (3) If non-zero, the most recent time an instance location was removed for this texture. */
	double			InstanceRemovedTimestamp;
	/** (3) Extra gameplay boost factor. Reset after every update. */
	float			DynamicBoostFactor;

	/** (4) How many mips are missing to satisfy ideal quality because of max size limitation. Used to prevent sacrificing mips that are already visually sacrificed. */
	int32			NumMissingMips;
	/** (4) Max wanted mips for visible instances.*/
	int32			VisibleWantedMips;
	/** (4) Wanted mips for non visible instances.*/
	int32			HiddenWantedMips;
	/** (4) Retention priority used to sacrifice mips when out of budget. */
	int32			RetentionPriority;
	/** (4) The max allowed mips (based on Visible and Hidden wanted mips) in order to fit in budget. */
	int32			BudgetedMips;
	/** (4) The load request priority. */
	int32			LoadOrderPriority;	
	/** (4) The mip that will be requested. Note, that some texture are loaded using split requests, so that the first request can be smaller than budgeted mip. */
	int32			WantedMips;
	/** (4) A persistent bias applied to this texture. Increase whenever the streamer needs to make sacrifices to fit in budget */
	int32			BudgetMipBias;
	/** (4) Number of LODs forced resident. Only used by meshes currently */
	int32			NumForcedMips;

	/** (5) */
	int32 CachedWantedMips;
	/** (5) */
	int32 CachedVisibleWantedMips;

	/** (1) */
	EAssetType RenderAssetType;
	/** (2) Cached state on disk of the optional mips for this streaming texture */
	EOptionalMipsState	OptionalMipsState;

	/** (1) Whether the texture is ready to be streamed in/out (cached from IsReadyForStreaming()). */
	uint32			bIsCharacterTexture : 1;
	/** (1) Whether the texture should be forcibly fully loaded. */
	uint32			bIsTerrainTexture : 1;

	/** (2) Whether the texture is ready to be streamed in/out (cached from IsReadyForStreaming()). */
	uint32			bReadyForStreaming : 1;
	/** (2) Whether the texture should be forcibly fully loaded. */
	uint32			bForceFullyLoad : 1;
	/** (2) Whether the texture resolution should be affected by the memory budget. */
	uint32			bIgnoreStreamingMipBias : 1;

	/** (3) Whether the texture is currently being streamed in/out. */
	uint32			bInFlight : 1;
	/** (3) Wheter the streamer has streaming plans for this texture. */
	uint32			bHasUpdatePending : 1;

	/** (4) Same as force fully load, but takes into account component settings. */
	uint32			bForceFullyLoadHeuristic : 1;
	/** (4) Whether this has not component referencing it. */
	uint32			bUseUnkownRefHeuristic : 1;
	/** (4) Same as force fully load, but takes into account component settings. */
	uint32			bLooksLowRes : 1;

	/** (5) */
	uint32			bCachedForceFullyLoadHeuristic : 1;

	/** (5) */
	TBitArray<>		LevelIndexUsage;

private:
	FORCEINLINE_DEBUGGABLE void StreamWantedMips_Internal(FRenderAssetStreamingManager& Manager, bool bUseCachedData);

	FORCEINLINE_DEBUGGABLE int32 ClampMaxResChange_Internal(int32 NumMipDropRequested) const;
};
