// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "GameplayTagContainer.h"
#include "PoseSearch/AnimNode_BlendStack.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "AnimNode_MotionMatching.generated.h"

class UPoseSearchDatabase;
class UPoseSearchSearchableAsset;

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_MotionMatching : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FPoseLink Source;

	// @todo: Delete this after updating content.
	// Collection of animations for motion matching.
	UPROPERTY(EditAnywhere, Category=Settings, meta=(PinHiddenByDefault))
	TObjectPtr<const UPoseSearchSearchableAsset> Searchable = nullptr;

	// The database to search. This can be overridden by Anim Node Functions such as "On Become Relevant" and "On Update" via SetDatabaseToSearch/SetDatabasesToSearch.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	TObjectPtr<const UPoseSearchDatabase> Database = nullptr;

	// Motion Trajectory samples for pose search queries in Motion Matching.These are expected to be in the space of the SkeletalMeshComponent.This is provided with the CharacterMovementTrajectory Component output.
	UPROPERTY(EditAnywhere, Category=Settings, meta=(PinShownByDefault))
	FTrajectorySampleRange Trajectory;

	// Settings for the core motion matching node.
	UPROPERTY(EditAnywhere, Category=Settings, meta=(PinHiddenByDefault))
	FMotionMatchingSettings Settings;

	// Reset the motion matching selection state if it has become relevant to the graph after not being updated on previous frames.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bResetOnBecomingRelevant = true;

	// If set to true, the continuing pose will be invalidated. This is useful if you want to force a re-selection of the animation segment instead of continuing with the previous segment, even if it has a better score.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bForceInterrupt = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDraw = false;

	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDrawQuery = true;

	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDrawMatch = true;
#endif

public:

	// FAnimNode_Base interface
	// @todo: implement CacheBones_AnyThread to rebind the schema bones
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// Search InDatabase instead of the Database property on this node. Use bForceInterruptIfNew to ignore the continuing pose if InDatabase is new.
	void SetDatabaseToSearch(UPoseSearchDatabase* InDatabase, bool bForceInterruptIfNew);

	// Search InDatabases instead of the Database property on the node. Use bForceInterruptIfNew to ignore the continuing pose if InDatabases is new.
	void SetDatabasesToSearch(const TArray<UPoseSearchDatabase*>& InDatabases, bool bForceInterruptIfNew);

	// Reset the effects of SetDatabaseToSearch/SetDatabasesToSearch and use the Database property on this node.
	void ResetDatabasesToSearch(bool bInForceInterrupt);

	// Ignore the continuing pose on the next update and force a search.
	void ForceInterruptNextUpdate();

private:

	FAnimNode_BlendStack_Standalone BlendStackNode;

	// Encapsulated motion matching algorithm and internal state
	FMotionMatchingState MotionMatchingState;

	// Update Counter for detecting being relevant
	FGraphTraversalCounter UpdateCounter;

	// List of databases this node is searching.
	UPROPERTY()
	TArray<TObjectPtr<const UPoseSearchDatabase>> DatabasesToSearch;

	// Ignore the continuing pose on the next update and use the best result from DatabasesToSearch. This is set back to false after each update.
	bool bForceInterruptNextUpdate = false;

	// True if the Database property on this node has been overridden by SetDatabaseToSearch/SetDatabasesToSearch.
	bool bOverrideDatabaseInput = false;

	// FAnimNode_AssetPlayerBase
protected:
	// FAnimNode_AssetPlayerBase interface
	virtual float GetAccumulatedTime() const override;
	virtual UAnimationAsset* GetAnimAsset() const override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual float GetCurrentAssetLength() const override;
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() const override;
	virtual bool GetIgnoreForRelevancyTest() const override;
	virtual bool SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest) override;
	// End of FAnimNode_AssetPlayerBase interface

private:

#if WITH_EDITORONLY_DATA
	// If true, "Relevant anim" nodes that look for the highest weighted animation in a state will ignore this node
	UPROPERTY(EditAnywhere, Category=Relevancy, meta=(FoldProperty, PinHiddenByDefault))
	bool bIgnoreForRelevancyTest = false;
#endif // WITH_EDITORONLY_DATA
};