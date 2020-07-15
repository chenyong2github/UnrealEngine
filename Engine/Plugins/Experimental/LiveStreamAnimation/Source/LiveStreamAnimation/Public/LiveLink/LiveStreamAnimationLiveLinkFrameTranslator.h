// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFrameTranslator.h"
#include "LiveStreamAnimationHandle.h"
#include "LiveStreamAnimationLiveLinkFrameTranslator.generated.h"

class USkeleton;

/**
 * Struct that defines how we can translate from incoming Live Stream Animation Live Link frames
 * onto live USkeletons.
 *
 * This is necessary for things like Quantization, Compression, and Stripping to work properly
 * as we won't have access to the Live Stream Animation frame data inside the Anim BP.
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
	 * Conceptually, this behaves similarly to a ULiveLinkRemapAsset, except we need this information
	 * up front to remap bones in case we need to grab Ref Bone Poses.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Live Stream Animation|Live Link|Translation")
	TMap<FName, FName> BoneRemappings;

	/**
	 * When non-empty, this is the full set of bones **from the Live Link Skeleton** that we will be receiving data
	 * for. This is only used as an optimization so we can cache bone indices for faster lookup.
	 * If this is empty, then we will fall back to using name based Map lookups, which is probably
	 * fine for most cases.
	 *
	 * This should contain the *exact* set of bones that will be needed from the Live Link Skeleton,
	 * in the exact order in which they will be sent from Live Link.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Live Stream Animation|Live Link|Translation")
	TArray<FName> BonesToUse;

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

	const FLiveStreamAnimationLiveLinkTranslationProfile* GetTranslationProfile(FName TranslationProfileName)
	{
		return TranslationProfiles.Find(TranslationProfileName);
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
	TMap<FName, FLiveStreamAnimationLiveLinkTranslationProfile> TranslationProfiles;

	FWorkerSharedPtr Worker;
};
