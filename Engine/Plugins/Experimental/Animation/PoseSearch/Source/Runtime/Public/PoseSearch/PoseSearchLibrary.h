// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AlphaBlend.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchResult.h"
#include "SequenceEvaluatorLibrary.h"
#include "SequencePlayerLibrary.h"
#include "PoseSearchLibrary.generated.h"

namespace UE::PoseSearch
{
	struct FSearchContext;
} // namespace UE::PoseSearch

struct FAnimationUpdateContext;
struct FTrajectorySampleRange;

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FMotionMatchingSettings
{
	GENERATED_BODY()

	// Time in seconds to blend out to the new pose. Uses either inertial blending, requiring an Inertialization node after this node, or the internal blend stack, if MaxActiveBlends is greater than zero.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float BlendTime = 0.2f;

	// Number of max active animation segments being blended together in the blend stack. If MaxActiveBlends is zero then the blend stack is disabled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	int32 MaxActiveBlends = 4;

	// Set Blend Profiles (editable in the skeleton) to determine how the blending is distributed among your character's bones. It could be used to differentiate between upper body and lower body to blend timing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(UseAsBlendProfile = true))
	TObjectPtr<UBlendProfile> BlendProfile;

	// How the blend is applied over time to the bones. Common selections are linear, ease in, ease out, and ease in and out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EAlphaBlendOption BlendOption = EAlphaBlendOption::Linear;

	// Don't jump to poses of the same segment that are less than this many seconds away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float PoseJumpThresholdTime = 0.f;

	// Prevent re-selection of poses that have been selected previously within this much time (in seconds) in the past. This is across all animation segments that have been selected within this time range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0"))
	float PoseReselectHistory = 0.3f;

	// Minimum amount of time to wait between searching for a new pose segment. It allows users to define how often the system searches, default for locomotion is searching every update, but you may only want to search once for other situations, like jump.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float SearchThrottleTime = 0.f;

	// Effective range of play rate that can be applied to the animations to account for discrepancies in estimated velocity between the movement modeland the animation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.5", ClampMax = "2.0", UIMin = "0.5", UIMax = "2.0"))
	FFloatInterval PlayRate = FFloatInterval(1.f, 1.f);
};

USTRUCT(BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FMotionMatchingState
{
	GENERATED_BODY()

	// Reset the state to a default state using the current Database
	void Reset();

	// Checks if the currently playing asset can advance and stay in bounds under the provided DeltaTime.
	bool CanAdvance(float DeltaTime) const;

	// Attempts to set the internal state to match the provided asset time including updating the internal DbPoseIdx. 
	// If the provided asset time is out of bounds for the currently playing asset then this function will reset the 
	// state back to the default state.
	void AdjustAssetTime(float AssetTime);

	// Internally stores the 'jump' to a new pose/sequence index and asset time for evaluation
	void JumpToPose(const FAnimationUpdateContext& Context, const FMotionMatchingSettings& Settings, const UE::PoseSearch::FSearchResult& Result);

	void UpdateWantedPlayRate(const UE::PoseSearch::FSearchContext& SearchContext, const FMotionMatchingSettings& Settings);

	UE::PoseSearch::FSearchResult CurrentSearchResult;

	// Time since the last pose jump
	UPROPERTY(Transient)
	float ElapsedPoseSearchTime = 0.f;

	// wanted PlayRate to have the selected animation playing at the estimated requested speed from the query.
	UPROPERTY(Transient)
	float WantedPlayRate = 1.f;

	// true if a new animation has been selected
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=State)
	bool bJumpedToPose = false;

	// Root motion delta for currently playing animation. Only required when UE_POSE_SEARCH_TRACE_ENABLED is active.
	UPROPERTY(Transient)
	FTransform RootMotionTransformDelta = FTransform::Identity;

	// @todo: add ContinuingPoseCost / BruteForcePoseCost / PoseCost graphs to rewind debugger 
//#if WITH_EDITORONLY_DATA
//	enum { SearchCostHistoryNumSamples = 200 };
//	FDebugFloatHistory SearchCostHistoryContinuing = FDebugFloatHistory(SearchCostHistoryNumSamples, 0, 0, true);
//	FDebugFloatHistory SearchCostHistoryBruteForce = FDebugFloatHistory(SearchCostHistoryNumSamples, 0, 0, true);
//	FDebugFloatHistory SearchCostHistoryKDTree = FDebugFloatHistory(SearchCostHistoryNumSamples, 0, 0, true);
//#endif

	UE::PoseSearch::FPoseIndicesHistory PoseIndicesHistory;
};

UCLASS()
class POSESEARCH_API UPoseSearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	static void TraceMotionMatchingState(
		const UPoseSearchDatabase* Database,
		UE::PoseSearch::FSearchContext& SearchContext,
		const UE::PoseSearch::FSearchResult& CurrentResult,
		const UE::PoseSearch::FSearchResult& LastResult,
		float ElapsedPoseSearchTime,
		const FTransform& RootMotionTransformDelta,
		const UObject* AnimInstance,
		int32 NodeId,
		float DeltaTime,
		bool bSearch);

public:
	/**
	* Implementation of the core motion matching algorithm
	*
	* @param Context						Input animation update context providing access to the proxy and delta time
	* @param Databases						Input array of databases to search
	* @param Trajectory						Input motion trajectory samples for pose search queries
	* @param Settings						Input motion matching algorithm configuration settings
	* @param InOutMotionMatchingState		Input/Output encapsulated motion matching algorithm and state
	* @param bForceInterrupt				Input force interrupt request (if true the continuing pose will be invalidated)
	*/
	static void UpdateMotionMatchingState(
		const FAnimationUpdateContext& Context,
		const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases,
		const FTrajectorySampleRange& Trajectory,
		const FMotionMatchingSettings& Settings,
		FMotionMatchingState& InOutMotionMatchingState,
		bool bForceInterrupt);

	/**
	* Implementation of the core motion matching algorithm
	*
	* @param AnimInstance					Input animation instance
	* @param Database						Input database to search
	* @param Trajectory						Input motion trajectory samples for pose search queries
	* @param PoseHistoryName				Input tag of the associated PoseSearchHistoryCollector node in the anim graph
	* @param SelectedAnimation				Output selected animation from the searchable asset
	* @param SelectedTime					Output selected animation time
	* @param bLoop							Output selected animation looping state
	* @param bIsMirrored					Output selected animation mirror state
	* @param BlendParameters				Output selected animation blend space parameters (if SelectedAnimation is a blend space)
	* @param SearchCost						Output search associated cost
	* @param FutureAnimation				Input animation we want to match after TimeToFutureAnimationStart seconds
	* @param FutureAnimationStartTime		Input start time for the first pose of FutureAnimation
	* @param TimeToFutureAnimationStart		Input time in seconds before start playing FutureAnimation (from FutureAnimationStartTime seconds)
	* @param DebugSessionUniqueIdentifier	Input unique identifier used to identify TraceMotionMatchingState (rewind debugger / pose search debugger) session. Similarly the MM node uses Context.GetCurrentNodeId()
	*/
	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe, Keywords = "PoseMatch"))
	static void MotionMatch(
		UAnimInstance* AnimInstance,
		const UPoseSearchDatabase* Database,
		const FTrajectorySampleRange Trajectory,
		const FName PoseHistoryName,
		UAnimationAsset*& SelectedAnimation,
		float& SelectedTime,
		bool& bLoop,
		bool& bIsMirrored,
		FVector& BlendParameters,
		float& SearchCost,
		const UAnimationAsset* FutureAnimation = nullptr,
		float FutureAnimationStartTime = 0.f,
		float TimeToFutureAnimationStart = 0.f,
		const int DebugSessionUniqueIdentifier = 6174);
};

