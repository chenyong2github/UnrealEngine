// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_BlendSpaceGraphBase.generated.h"

class UBlendSpaceBase;

// Allows multiple animations to be blended between based on input parameters
USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_BlendSpaceGraphBase : public FAnimNode_Base
{
	GENERATED_BODY()

	friend class UAnimGraphNode_BlendSpaceGraphBase;

	// @return the blendspace that this node uses
	const UBlendSpaceBase* GetBlendSpace() const { return BlendSpace; }

	// @return the current sample coordinates that this node is using to sample the blendspace
	FVector GetPosition() const { return FVector(X, Y, Z); }

	// @return the current sample coordinates after going through the filtering
	FVector GetFilteredPosition() const { return BlendFilter.GetFilterLastOutput(); }

#if WITH_EDITORONLY_DATA
	// Set the node to preview a supplied sample value
	void SetPreviewPosition(FVector InVector);
#endif

protected:
	// The X coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Coordinates, meta = (PinShownByDefault, AllowPrivateAccess))
	float X = 0.0f;

	// The Y coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Coordinates, meta = (PinShownByDefault, AllowPrivateAccess))
	float Y = 0.0f;

	// The Z coordinate to sample in the blendspace
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Coordinates, meta = (PinHiddenByDefault, AllowPrivateAccess))
	float Z = 0.0f;

	// The group name that we synchronize with. All nodes employing sync beyond this in the anim graph will implicitly use this sync group.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sync, meta = (NeverAsPin, AllowPrivateAccess))
	FName GroupName = NAME_None;

	// The role this player can assume within the group
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sync, meta = (NeverAsPin, AllowPrivateAccess))
	TEnumAsByte<EAnimGroupRole::Type> GroupRole = EAnimGroupRole::CanBeLeader;

	// The internal blendspace asset to play
	UPROPERTY()
	TObjectPtr<const UBlendSpaceBase> BlendSpace = nullptr;

	// Pose links for each sample in the blendspace
	UPROPERTY()
	TArray<FPoseLink> SamplePoseLinks;

protected:
	// FIR filter applied to inputs
	FBlendFilter BlendFilter;

	// Cache of sampled data, updated each frame
	TArray<FBlendSampleData> BlendSampleDataCache;

#if WITH_EDITORONLY_DATA
	// Preview blend params - set in editor only
	FVector PreviewPosition = FVector::ZeroVector;

	// Whether to use the preview blend params
	bool bUsePreviewPosition = false;
#endif

	// Internal update handler, skipping evaluation of exposed inputs
	void UpdateInternal(const FAnimationUpdateContext& Context);

protected:
	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface
};
