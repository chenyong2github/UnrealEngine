// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "AnimNode_BlendSpacePlayer.generated.h"

class UBlendSpace;

//@TODO: Comment
USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_BlendSpacePlayer : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

 	friend class UAnimGraphNode_BlendSpacePlayer;
 	friend class UAnimGraphNode_BlendSpaceEvaluator;
	friend class UAnimGraphNode_RotationOffsetBlendSpace;
	friend class UAnimGraphNode_AimOffsetLookAt;

	// @return the current sample coordinates after going through the filtering
	FVector GetFilteredPosition() const { return BlendFilter.GetFilterLastOutput(); }
private:

#if WITH_EDITORONLY_DATA
	// The group name (NAME_None if it is not part of any group)
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	FName GroupName = NAME_None;

	// The role this player can assume within the group (ignored if GroupIndex is INDEX_NONE)
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	TEnumAsByte<EAnimGroupRole::Type> GroupRole = EAnimGroupRole::CanBeLeader;

	// How synchronization is determined
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	EAnimSyncMethod Method = EAnimSyncMethod::DoNotSync;

	// If true, "Relevant anim" nodes that look for the highest weighted animation in a state will ignore this node
	UPROPERTY(EditAnywhere, Category=Relevancy, meta=(FoldProperty, PinHiddenByDefault))
	bool bIgnoreForRelevancyTest = false;
	
	// The X coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, Category=Coordinates, meta=(PinShownByDefault, FoldProperty))
	float X = 0.0f;

	// The Y coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, Category = Coordinates, meta = (PinShownByDefault, FoldProperty))
	float Y = 0.0f;

	// The Z coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, Category = Coordinates, meta = (PinHiddenByDefault, FoldProperty))
	float Z = 0.0f;

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (DefaultValue = "1.0", PinHiddenByDefault, FoldProperty))
	float PlayRate = 1.0f;

	// Should the animation continue looping when it reaches the end?
	UPROPERTY(EditAnywhere, Category = Settings, meta = (DefaultValue = "true", PinHiddenByDefault, FoldProperty))
	bool bLoop = true;

	// Whether we should reset the current play time when the blend space changes
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	bool bResetPlayTimeWhenBlendSpaceChanges = true;

	// The start up position in [0, 1], it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	UPROPERTY(EditAnywhere, Category=Settings, meta = (DefaultValue = "0.f", PinHiddenByDefault, FoldProperty))
	float StartPosition = 0.0f;

	// The blendspace asset to play
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, FoldProperty))
	TObjectPtr<UBlendSpace> BlendSpace = nullptr;
#endif

protected:
	// Filter used to dampen coordinate changes
	FBlendFilter BlendFilter;

	// Cache of samples used to determine blend weights
	TArray<FBlendSampleData> BlendSampleDataCache;

	/** Previous position in the triangulation/segmentation */
	int32 CachedTriangulationIndex = -1;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> PreviousBlendSpace = nullptr;

public:	
	FAnimNode_BlendSpacePlayer() = default;

	// FAnimNode_AssetPlayerBase interface
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() const override;
	virtual float GetCurrentAssetLength() const override;
	virtual UAnimationAsset* GetAnimAsset() const override;
	virtual FName GetGroupName() const override;
	virtual EAnimGroupRole::Type GetGroupRole() const override;
	virtual EAnimSyncMethod GetGroupMethod() const override;
	virtual bool GetIgnoreForRelevancyTest() const override;
	virtual bool SetGroupName(FName InGroupName) override;
	virtual bool SetGroupRole(EAnimGroupRole::Type InRole) override;
	virtual bool SetGroupMethod(EAnimSyncMethod InMethod) override;
	virtual bool SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest) override;
	// End of FAnimNode_AssetPlayerBase interface

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	float GetTimeFromEnd(float CurrentTime) const;

	// Set the blendspace asset to play
	bool SetBlendSpace(UBlendSpace* InBlendSpace);

	// Get the coordinates that are currently being sampled by the blendspace
	virtual FVector GetPosition() const;

	// Get the play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	float GetPlayRate() const;

	// Should the animation continue looping when it reaches the end?
	bool GetLoop() const;

	// Get whether we should reset the current play time when the blend space changes
	bool ShouldResetPlayTimeWhenBlendSpaceChanges() const;

	// Get the start up position in [0, 1], it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	float GetStartPosition() const;

	// Get the blendspace asset to play
	UBlendSpace* GetBlendSpace() const;

protected:
	void UpdateInternal(const FAnimationUpdateContext& Context);

private:
	void Reinitialize(bool bResetTime = true);

	const FBlendSampleData* GetHighestWeightedSample() const;
};
