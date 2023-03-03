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
class UPoseSearchSearchableAsset;

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FMotionMatchingSettings
{
	GENERATED_BODY()

	// Time in seconds to blend out to the new pose. Uses either inertial blending, requiring an Inertialization node after this node, or the internal blend stack, if MaxActiveBlends is greter than zero
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float BlendTime = 0.2f;

	// Number of max active blendin animation in the blend stack. If MaxActiveBlends is zero then blend stack is disabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	int32 MaxActiveBlends = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(UseAsBlendProfile = true))
	TObjectPtr<UBlendProfile> BlendProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EAlphaBlendOption BlendOption = EAlphaBlendOption::Linear;

	// If the pose jump requires a mirroring change and this value is greater than 0, it will be used instead of BlendTime
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0", DislayAfter="BlendTime"))
	float MirrorChangeBlendTime = 0.0f;
	
	// Don't jump to poses that are less than this many seconds away
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float PoseJumpThresholdTime = 0.f;

	// Don't jump to poses that has been selected previously within this many seconds in the past
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0"))
	float PoseReselectHistory = 0.3f;

	// Minimum amount of time to wait between pose search queries
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float SearchThrottleTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.5", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
	float PlayRateMin = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "1.0", ClampMax = "2.0", UIMin = "1.0", UIMax = "2.0"))
	float PlayRateMax = 1.f;
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

	float ComputeJumpBlendTime(const UE::PoseSearch::FSearchResult& Result, const FMotionMatchingSettings& Settings) const;

	void UpdateWantedPlayRate(const UE::PoseSearch::FSearchContext& SearchContext, const FMotionMatchingSettings& Settings);

	UE::PoseSearch::FSearchResult CurrentSearchResult;

	// Time since the last pose jump
	UPROPERTY(Transient)
	float ElapsedPoseSearchTime = 0.f;

	// wanted PlayRate to have the selected animation playing at the estimated requested speed from the query
	UPROPERTY(Transient)
	float WantedPlayRate = 1.f;

	// true if a new animation has been selected
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=State)
	bool bJumpedToPose = false;

	// Root motion delta for currently playing animation. Only required
	// when UE_POSE_SEARCH_TRACE_ENABLED is active
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
		const UPoseSearchSearchableAsset* Searchable,
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
	* @param Searchable						Input collection of animations for motion matching
	* @param Trajectory						Input motion trajectory samples for pose search queries
	* @param Settings						Input motion matching algorithm configuration settings
	* @param InOutMotionMatchingState		Input/Output encapsulated motion matching algorithm and state
	* @param bForceInterrupt				Input force interrupt request (if true the continuing pose will be invalidated)
	*/
	static void UpdateMotionMatchingState(
		const FAnimationUpdateContext& Context,
		const UPoseSearchSearchableAsset* Searchable,
		const FTrajectorySampleRange& Trajectory,
		const FMotionMatchingSettings& Settings,
		FMotionMatchingState& InOutMotionMatchingState,
		bool bForceInterrupt);

	/**
	* Implementation of the core motion matching algorithm
	*
	* @param AnimInstance					Input animation instance
	* @param Searchable						Input searchable asset
	* @param Trajectory						Input motion trajectory samples for pose search queries
	* @param PoseHistoryName				Input tag of the associated PoseSearchHistoryCollector node in the anim graph
	* @param SelectedAnimation				Output selected animation from the searchable asset
	* @param SelectedTime					Output selected animation time
	* @param bLoop							Output selected animation looping state
	* @param bIsMirrored					Output selected animation mirror state
	* @param BlendParameters				Output selected animation blend space parameters (if SelectedAnimation is a blend space)
	* @param FutureAnimation				Input animation we want to match after TimeToFutureAnimationStart seconds
	* @param FutureAnimationStartTime		Input start time for the first pose of FutureAnimation
	* @param TimeToFutureAnimationStart		Input time in seconds before start playing FutureAnimation (from FutureAnimationStartTime seconds)
	* @param DebugSessionUniqueIdentifier	Input unique identifier used to identify TraceMotionMatchingState (rewind debugger / pose search debugger) session. Similarly the MM node uses Context.GetCurrentNodeId()
	*/
	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe, Keywords = "PoseMatch"))
	static void MotionMatch(
		UAnimInstance* AnimInstance,
		const UPoseSearchSearchableAsset* Searchable,
		const FTrajectorySampleRange Trajectory,
		const FName PoseHistoryName,
		UAnimationAsset*& SelectedAnimation,
		float& SelectedTime,
		bool& bLoop,
		bool& bIsMirrored,
		FVector& BlendParameters,
		const UAnimationAsset* FutureAnimation = nullptr,
		float FutureAnimationStartTime = 0.f,
		float TimeToFutureAnimationStart = 0.f,
		const int DebugSessionUniqueIdentifier = 6174);
};

