// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/App.h"
#include "StreamableRenderAsset.generated.h"

#define STREAMABLERENDERASSET_NODEFAULT(FuncName) LowLevelFatalError(TEXT("UStreamableRenderAsset::%s has no default implementation"), TEXT(#FuncName))

UCLASS(Abstract, MinimalAPI)
class UStreamableRenderAsset : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Get an integer representation of the LOD group */
	virtual int32 GetLODGroupForStreaming() const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetLODGroupForStreaming);
		return -1;
	}

	/** Get the total number of LODs including non-streamable and optional LODs */
	virtual int32 GetNumMipsForStreaming() const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetNumMipsForStreaming);
		return -1;
	}

	/** Returns the number of LODs in this asset that are not able to be streamed */
	virtual int32 GetNumNonStreamingMips() const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetNumNonStreamingMips);
		return -1;
	}

	virtual int32 CalcNumOptionalMips() const
	{
		STREAMABLERENDERASSET_NODEFAULT(CalcNumOptionalMips);
		return -1;
	}

	virtual int32 CalcCumulativeLODSize(int32 NumLODs) const
	{
		STREAMABLERENDERASSET_NODEFAULT(CalcCumulativeLODSize);
		return -1;
	}

	virtual bool GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetMipDataFilename);
		return false;
	}

	virtual bool IsReadyForStreaming() const
	{
		STREAMABLERENDERASSET_NODEFAULT(IsReadyForStreaming);
		return false;
	}

	/** The number of LODs currently in memory */
	virtual int32 GetNumResidentMips() const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetNumResidentMips);
		return -1;
	}

	/** When the asset is being updated from StreamIn() or StreamOut(), returns the number of LODs requested */
	virtual int32 GetNumRequestedMips() const
	{
		STREAMABLERENDERASSET_NODEFAULT(GetNumRequestedMips);
		return -1;
	}

	/**
	* Tries to cancel a pending LOD change request. Requests cannot be canceled if they are in the
	* finalization phase.
	*
	* @param	true if cancelation was successful, false otherwise
	*/
	virtual bool CancelPendingMipChangeRequest()
	{
		STREAMABLERENDERASSET_NODEFAULT(CancelPendingMipChangeRequest);
		return false;
	}

	virtual bool HasPendingUpdate() const
	{
		STREAMABLERENDERASSET_NODEFAULT(HasPendingUpdate);
		return false;
	}

	virtual bool IsPendingUpdateLocked() const 
	{ 
		STREAMABLERENDERASSET_NODEFAULT(IsPendingUpdateLocked);
		return false;
	}

	/**
	* Unload some mips from memory. Only usable if the asset is streamable.
	*
	* @param NewMipCount - The desired mip count after the mips are unloaded.
	* @return Whether any mips were requested to be unloaded.
	*/
	virtual bool StreamOut(int32 NewMipCount)
	{
		STREAMABLERENDERASSET_NODEFAULT(StreamOut);
		return false;
	}

	/**
	* Loads mips from disk to memory. Only usable if the asset is streamable.
	*
	* @param NewMipCount - The desired mip count after the mips are loaded.
	* @param bHighPrio   - true if the load request is of high priority and must be issued before other asset requests.
	* @return Whether any mips were resquested to be loaded.
	*/
	virtual bool StreamIn(int32 NewMipCount, bool bHighPrio)
	{
		STREAMABLERENDERASSET_NODEFAULT(StreamIn);
		return false;
	}

	/**
	* Updates the streaming status of the asset and performs finalization when appropriate. The function returns
	* true while there are pending requests in flight and updating needs to continue.
	*
	* @param bWaitForMipFading	Whether to wait for Mip Fading to complete before finalizing.
	* @return					true if there are requests in flight, false otherwise
	*/
	virtual bool UpdateStreamingStatus(bool bWaitForMipFading = false)
	{
		STREAMABLERENDERASSET_NODEFAULT(UpdateStreamingStatus);
		return false;
	}

	/**
	* Invalidates per-asset last render time. Mainly used to opt in UnknownRefHeuristic
	* during LOD index calculation. See FStreamingRenderAsset::bUseUnknownRefHeuristic
	*/
	virtual void InvalidateLastRenderTimeForStreaming() {}

	/**
	* Get the per-asset last render time. FLT_MAX means never use UnknownRefHeuristic
	* and the asset will only keep non-streamable LODs when there is no instance/reference
	* in the scene
	*/
	virtual float GetLastRenderTimeForStreaming() const { return FLT_MAX; }

	/**
	* Returns whether miplevels should be forced resident.
	*
	* @return true if either transient or serialized override requests miplevels to be resident, false otherwise
	*/
	virtual bool ShouldMipLevelsBeForcedResident() const
	{
		return bGlobalForceMipLevelsToBeResident
			|| bForceMiplevelsToBeResident
			|| ForceMipLevelsToBeResidentTimestamp >= FApp::GetCurrentTime();
	}

	/**
	* Tells the streaming system that it should force all mip-levels to be resident for a number of seconds.
	* @param Seconds					Duration in seconds
	* @param CinematicTextureGroups	Bitfield indicating which texture groups that use extra high-resolution mips
	*/
	ENGINE_API void SetForceMipLevelsToBeResident(float Seconds, int32 CinematicLODGroupMask = 0);

	/**
	* Returns the cached combined LOD bias based on texture LOD group and LOD bias.
	* @return	LOD bias
	*/
	ENGINE_API int32 GetCachedLODBias() const { return CachedCombinedLODBias; }

	FORCEINLINE void SetCachedNumResidentLODs(uint8 NewVal)
	{
#if !(WITH_EDITOR)
		CachedNumResidentLODs = NewVal;
#endif
	}

	FORCEINLINE void SetCachedReadyForStreaming(bool NewVal)
	{
#if !(WITH_EDITOR)
		bCachedReadyForStreaming = NewVal;
#endif
	}

	FORCEINLINE uint8 GetCachedNumResidentLODs() const
	{
#if WITH_EDITOR
		return GetNumResidentMips();
#else
		return CachedNumResidentLODs;
#endif
	}

	FORCEINLINE bool GetCachedReadyForStreaming() const
	{
#if WITH_EDITOR
		return IsReadyForStreaming();
#else
		return bCachedReadyForStreaming;
#endif
	}

protected:
	/** WorldSettings timestamp that tells the streamer to force all miplevels to be resident up until that time. */
	UPROPERTY(transient)
	double ForceMipLevelsToBeResidentTimestamp;

public:
	/** Number of mip-levels to use for cinematic quality. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LevelOfDetail, AdvancedDisplay)
	int32 NumCinematicMipLevels;

protected:
	/** FStreamingRenderAsset index used by the texture streaming system. */
	UPROPERTY(transient, duplicatetransient, NonTransactional)
	int32 StreamingIndex;

	/** Cached combined group and texture LOD bias to use.	*/
	UPROPERTY(transient)
	int32 CachedCombinedLODBias;

	/** Cached value of GetNumResidentMips(). Used to reduce cache misses */
	UPROPERTY(transient)
	uint8 CachedNumResidentLODs;

	/** Cached value of IsReadyForStreaming(). Used to reduce cache misses */
	UPROPERTY(transient)
	uint8 bCachedReadyForStreaming : 1;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LevelOfDetail, AssetRegistrySearchable, AdvancedDisplay)
	uint8 NeverStream : 1;

	/** Global and serialized version of ForceMiplevelsToBeResident.				*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = LevelOfDetail, meta = (DisplayName = "Global Force Resident Mip Levels"), AdvancedDisplay)
	uint8 bGlobalForceMipLevelsToBeResident : 1;

	/** Whether the texture is currently streamable or not.						*/
	UPROPERTY(transient, NonTransactional)
	uint8 bIsStreamable : 1;

	/** Whether some mips might be streamed soon. If false, the texture is not planned resolution will be stable. */
	UPROPERTY(transient, NonTransactional)
	uint8 bHasStreamingUpdatePending : 1;

	/** Override whether to fully stream even if texture hasn't been rendered.	*/
	UPROPERTY(transient)
	uint8 bForceMiplevelsToBeResident : 1;

	/** Ignores the streaming mip bias used to accommodate memory constraints. */
	UPROPERTY(transient)
	uint8 bIgnoreStreamingMipBias : 1;

protected:
	/** Whether to use the extra cinematic quality mip-levels, when we're forcing mip-levels to be resident. */
	UPROPERTY(transient)
	uint8 bUseCinematicMipLevels : 1;

	friend struct FRenderAssetStreamingManager;
	friend struct FStreamingRenderAsset;
};
