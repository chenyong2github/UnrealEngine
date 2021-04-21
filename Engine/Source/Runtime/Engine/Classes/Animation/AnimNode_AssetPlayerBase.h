// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_AssetPlayerBase.generated.h"

/* Base class for any asset playing anim node */
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_AssetPlayerBase : public FAnimNode_Base
{
	GENERATED_BODY();

	friend class UAnimGraphNode_AssetPlayerBase;

	FAnimNode_AssetPlayerBase() = default;

	/** Get the last encountered blend weight for this node */
	virtual float GetCachedBlendWeight() const;
	
	/** Set the cached blendweight to zero */
	void ClearCachedBlendWeight();

	/** Get the currently referenced time within the asset player node */
	virtual float GetAccumulatedTime() const;

	/** Override the currently accumulated time */
	virtual void SetAccumulatedTime(const float& NewTime);

	/** Get the animation asset associated with the node, derived classes should implement this */
	virtual UAnimationAsset* GetAnimAsset() const;

	/** Initialize function for setup purposes */
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;

	/** Update the node, marked final so we can always handle blendweight caching.
	 *  Derived classes should implement UpdateAssetPlayer
	 */

	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) final override;

	/** Update method for the asset player, to be implemented by derived classes */
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) {};

	// Create a tick record for this node
	void CreateTickRecordForNode(const FAnimationUpdateContext& Context, UAnimSequenceBase* Sequence, bool bLooping, float PlayRate);

	// Functions to report data to getters, this is required for all asset players (but can't be pure abstract because of struct instantiation generated code).
	virtual float GetCurrentAssetLength() const { return 0.0f; }
	virtual float GetCurrentAssetTime() const { return 0.0f; }
	virtual float GetCurrentAssetTimePlayRateAdjusted() const { return GetCurrentAssetTime(); }

	// Get the sync group name we are using
	FName GetGroupName() const;

	// Get the sync group role we are using
	EAnimGroupRole::Type GetGroupRole() const;

	// Get the sync group method we are using
	EAnimSyncMethod GetGroupMethod() const;

	// Check whether this node should be ignored when testing for relevancy in state machines
	bool GetIgnoreForRelevancyTest() const;

#if WITH_EDITORONLY_DATA
	// Set the sync group name we are using
	void SetGroupName(FName InGroupName) { GroupName = InGroupName; }

	// Set the sync group role we are using
	void GetGroupRole(EAnimGroupRole::Type InRole) { GroupRole = InRole; }

	// Set the sync group method we are using
	void SetGroupMethod(EAnimSyncMethod InMethod) { Method = InMethod; }

	// Set whether this node should be ignored when testing for relevancy in state machines
	void SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest) { bIgnoreForRelevancyTest = bInIgnoreForRelevancyTest; }
#endif

private:

#if WITH_EDITORONLY_DATA
	// The group name (NAME_None if it is not part of any group)
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	FName GroupName = NAME_None;

	UPROPERTY()
	int32 GroupIndex_DEPRECATED = INDEX_NONE;

	UPROPERTY()
	EAnimSyncGroupScope GroupScope_DEPRECATED = EAnimSyncGroupScope::Local;

	// The role this player can assume within the group (ignored if GroupIndex is INDEX_NONE)
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	TEnumAsByte<EAnimGroupRole::Type> GroupRole = EAnimGroupRole::CanBeLeader;

	// How synchronization is determined
	UPROPERTY(EditAnywhere, Category=Sync, meta=(FoldProperty))
	EAnimSyncMethod Method = EAnimSyncMethod::DoNotSync;

	/** If true, "Relevant anim" nodes that look for the highest weighted animation in a state will ignore
	 *  this node
	 */
	UPROPERTY(EditAnywhere, Category=Relevancy, meta=(FoldProperty, PinHiddenByDefault))
	bool bIgnoreForRelevancyTest = false;
#endif

protected:
	/** Store data about current marker position when using marker based syncing*/
	FMarkerTickRecord MarkerTickRecord;

	/** Last encountered blendweight for this node */
	UPROPERTY(BlueprintReadWrite, Transient, Category=DoNotEdit)
	float BlendWeight = 0.0f;

	/** Accumulated time used to reference the asset in this node */
	UPROPERTY(BlueprintReadWrite, Transient, Category=DoNotEdit)
	float InternalTimeAccumulator = 0.0f;

	/** Track whether we have been full weight previously. Reset when we reach 0 weight*/
	bool bHasBeenFullWeight = false;
};
