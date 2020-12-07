// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNode_MotionMatching.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_MotionMatching : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPoseLink Source;

	// Capsule local velocity from inputs
	// Note this is temporary, we'll need a component to supply future trajectory according to the database schema
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	FVector LocalVelocity = FVector::ZeroVector;

	// Collection of animations for motion matching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	const UPoseSearchDatabase* Database = nullptr;

	// Time in seconds to blend out to the new pose. Uses inertial blending and requires an Inertialization node after this node.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float BlendTime = 0.1f;

	// Ignore pose candidates within the active animation that are less than PoseJumpThreshold seconds away from the current asset player time
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float PoseJumpThreshold = 0.5f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Debug)
	bool bDebugDraw = false;
#endif

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:

	// Embedded sequence player node for playing animations from the motion matching database
	FAnimNode_SequencePlayer SequencePlayerNode;

	// The current pose we're playing from the database
	int32 DbPoseIdx = INDEX_NONE;

	// The current animation we're playing from the database
	int32 DbSequenceIdx = INDEX_NONE;

	// The current query feature vector used to search the database for pose candidates
	UE::PoseSearch::FFeatureVectorBuilder QueryBuilder;
	TArray<float> Query;

	bool IsValidForSearch() const;
	void SetTrajectoryFeatures();
	void JumpToPose(const FAnimationUpdateContext& Context, UE::PoseSearch::FDbSearchResult Result);
};