// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFrameTranslator.h"
#include "LiveStreamAnimationHandle.h"
#include "LiveStreamAnimationLiveLinkFrameTranslator.generated.h"

class USkeleton;

/**
 * A single translation profile that can map one Live Link Subject Skeleton onto one UE Skeleton.
 */
USTRUCT(BlueprintType)
struct LIVESTREAMANIMATION_API FLiveStreamAnimationLiveLinkTranslationProfile
{
	GENERATED_BODY()

public:

	/**
	 * The USkeleton that is associated with this profile.
	 * This is necessary so we can grab Ref Bone Poses when we are only sending partial transforms.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Live Stream Animation|Live Link|Translation")
	TSoftObjectPtr<USkeleton> Skeleton;

	/**
	 * Map from Skeleton Bone Name to Live Link Subject Bone Name.
	 * Only bones that have inconsistent naming between the UE Skeleton and the Live Link Skeleton (static data)
	 * need to have entries.
	 *
	 * Every bone name in the skeleton needs to be unique, so remapping multiple source bones onto the same target bone
	 * (i.e. different keys onto the same value) or remapping a source bone onto a target bone that already exists
	 * in the skeleton that is not also remapped will cause issues.
	 *
	 * Conceptually, this behaves similarly to a ULiveLinkRemapAsset, except we need this information
	 * up front to remap bones in case we need to grab Ref Bone Poses.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Live Stream Animation|Live Link|Translation")
	TMap<FName, FName> BoneRemappings;

	/**
	 * When non-empty, this is the full set of bones **from the Live Link Skeleton** for which we will
	 * be receiving data This is only used as an optimization so we can cache bone indices for faster lookup.
	 * If this is empty, then we will fall back to using name based Map lookups, which is probably
	 * fine for most cases.
	 *
	 * This should contain the *exact* set of bones that will be needed from the Live Link Skeleton,
	 * in the exact order in which they will be sent from Live Link.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Live Stream Animation|Live Link|Translation")
	TArray<FName> BonesToUse;

	/**
	 * When this is true, before we stream any Live Link Data, we will strip it down to just the
	 * bones specified in BonesToUse.
	 *
	 * This is mainly useful when there are large Live Link Rigs that only need to replicate a
	 * subset of their bones for proper Animation Streaming.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Live Stream Animation|Live Link|Translation")
	bool bStripLiveLinkSkeletonToBonesToUse = false;

	const TMap<FName, FTransform>& GetBoneTransformsByName() const
	{
		return BoneTransformsByName;
	}

	const TArray<FTransform>& GetBoneTransformsByIndex() const
	{
		return BoneTransformsByIndex;
	}

	bool UpdateTransformMappings();

private:

	// TODO: This could probably be cached off when cooking.
	/** Bone transforms by name that will be used if BonesToUse is not specified, or seems invalid. */
	TMap<FName, FTransform> BoneTransformsByName;

	// TODO: This could probably be cached off when cooking.
	/** Bone transforms by bone index that will be used if BonesToUse is specified and valid. */
	TArray<FTransform> BoneTransformsByIndex;
};

/**
 * Class that defines how we can translate incoming Live Stream Skeletons
 * onto live UE Skeletons.
 *
 * Individual translations are defined as FLiveStreamAnimationLiveLinkTranslationProfiles.
 *
 * This is necessary for things like Quantization, Compression, and Stripping unused to work properly
 * as we won't have access to the Live Stream Animation frame data inside the Anim BP, and therefore
 * need to preprocess the network data.
 *
 * This could also be changed so we delay the processing of packets completely until we know they
 * will be used.
 */
UCLASS(Blueprintable, BlueprintType, Config=Game, ClassGroup = (LiveStreamAnimation))
class LIVESTREAMANIMATION_API ULiveStreamAnimationLiveLinkFrameTranslator : public ULiveLinkFrameTranslator
{
	GENERATED_BODY()

public:

	using FWorkerSharedPtr = ULiveLinkFrameTranslator::FWorkerSharedPtr;

	//~ Begin ULiveLinkFrameTranslator Interface
	virtual TSubclassOf<ULiveLinkRole> GetFromRole() const override;
	virtual TSubclassOf<ULiveLinkRole> GetToRole() const override;
	virtual FWorkerSharedPtr FetchWorker() override;
	//~ End ULiveLinkFrameTranslator Interface

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif

	const FLiveStreamAnimationLiveLinkTranslationProfile* GetTranslationProfile(FLiveStreamAnimationHandle TranslationProfileHandle) const
	{
		return GetTranslationProfile(FLiveStreamAnimationHandleWrapper(TranslationProfileHandle));
	}

	const FLiveStreamAnimationLiveLinkTranslationProfile* GetTranslationProfile(FName TranslationProfileHandleName) const
	{
		return GetTranslationProfile(FLiveStreamAnimationHandleWrapper(TranslationProfileHandleName));
	}

	const FLiveStreamAnimationLiveLinkTranslationProfile* GetTranslationProfile(FLiveStreamAnimationHandleWrapper TranslationProfileHandle) const
	{
		return TranslationProfiles.Find(TranslationProfileHandle);
	}

private:

	/**
	 * Map of Name to Translation profile.
	 * Each name used *must* be a valid LiveStreamAnimationHandle name, or that entry will be ignored.
	 *
	 * See @FLiveStreamAnimationHandle.
	 * See @ULiveStreamAnimationSubsystem::HandleNames.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Live Stream Animation|Live Link|Translation")
	TMap<FLiveStreamAnimationHandleWrapper, FLiveStreamAnimationLiveLinkTranslationProfile> TranslationProfiles;

	FWorkerSharedPtr Worker;
};
