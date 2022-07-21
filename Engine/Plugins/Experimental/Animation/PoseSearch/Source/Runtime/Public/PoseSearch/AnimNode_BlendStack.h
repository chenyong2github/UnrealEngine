// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Mirror.h"
#include "Containers/Deque.h"
#include "PoseSearch/PoseSearch.h"

#include "AnimNode_BlendStack.generated.h"

USTRUCT()
struct FPoseSearchAnimPlayer
{
	GENERATED_BODY()
	
	void Initialize(ESearchIndexAssetType InAssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, float BlendTime = 0.f, FVector BlendParameters = FVector::Zero());
	void Evaluate_AnyThread(FPoseContext& Output);
	void Update_AnyThread(const FAnimationUpdateContext& Context);
	float GetAccumulatedTime() const;
	float GetBlendWeight() const { return BlendWeight;  }
	float GetBlendInPercentage() const;
	void SetBlendWeight(float InBlendWeight);
	ESearchIndexAssetType GetAssetType() const { return AssetType; }

	// @todo: used only for DynamicPlayRateAdjustment. Remove once the functionality is integrated with the BlendStackNode
	FAnimNode_SequencePlayer_Standalone GetSequencePlayerNode() { return SequencePlayerNode; }

protected:
	void UpdateSourceLinkNode();

	// @todo: maybe use an union between the standalone players?
	
	// Embedded sequence player node for playing animations from the motion matching database
	FAnimNode_SequencePlayer_Standalone SequencePlayerNode;

	// Embedded blendspace player node for playing blendspaces from the motion matching database
	FAnimNode_BlendSpacePlayer_Standalone BlendSpacePlayerNode;

	// Embedded mirror node to handle mirroring if the pose search results in a mirrored sequence
	FAnimNode_Mirror_Standalone MirrorNode;

	ESearchIndexAssetType AssetType = ESearchIndexAssetType::Sequence;

	float TotalBlendInTime = 0.f;
	float CurrentBlendInTime = 0.f;
	float BlendWeight = 1.f;
};

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_BlendStack : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

	TDeque<FPoseSearchAnimPlayer> AnimPlayers;

	// FAnimNode_Base interface
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	// End of FAnimNode_Base interface

	void BlendTo(ESearchIndexAssetType AssetType, UAnimationAsset* AnimationAsset, float AccumulatedTime, bool bLoop, bool bMirrored, UMirrorDataTable* MirrorDataTable, int32 MaxActiveBlends = 3, float BlendTime = 0.3f, FVector BlendParameters = FVector::Zero());
	void CalculateWeights();
	void PruneBlendStack(int32 MaxActiveBlends);

	// FAnimNode_AssetPlayerBase interface
	virtual float GetAccumulatedTime() const override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	// End of FAnimNode_AssetPlayerBase interface
};
