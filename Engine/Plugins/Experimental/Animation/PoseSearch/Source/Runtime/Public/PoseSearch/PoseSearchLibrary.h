// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearch.h"
#include "SequenceEvaluatorLibrary.h"
#include "SequencePlayerLibrary.h"

#include "PoseSearchLibrary.generated.h"

namespace UE::PoseSearch
{
	struct FMotionMatchingContinuityParams
	{
		FDbSearchResult Result;
		bool bJumpRequired = false;

		bool IsValid()
		{
			return Result.IsValid();
		}

		void Reset()
		{
			Result = UE::PoseSearch::FDbSearchResult();
			bJumpRequired = false;
		}
	};
}

UENUM(BlueprintType, Category="Motion Trajectory", meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EMotionMatchingFlags : uint8
{
	None = 0 UMETA(Hidden),
	JumpedToPose = 1 << 0 // Signals that motion matching has made a significant deviation in the selected sequence/pose index
};
ENUM_CLASS_FLAGS(EMotionMatchingFlags);

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FMotionMatchingSettings
{
	GENERATED_BODY()

	// Dynamic weights for influencing pose selection
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	FPoseSearchDynamicWeightParams Weights;

	// Time in seconds to blend out to the new pose. Uses inertial blending and requires an Inertialization node after this node.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float BlendTime = 0.2f;
	
	// Don't jump to poses that are less than this many seconds away
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float PoseJumpThresholdTime = 1.f;

	// Minimum amount of time to wait between pose search queries
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float SearchThrottleTime = 0.1f;

	// How much better the search result must be compared to the current pose in order to jump to it
	// Note: This feature won't work quite as advertised until search data rescaling is implemented
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0", ClampMax="100"))
	float MinPercentImprovement = 100.f;

	// Pose indices SequenceEndExclusionTime or less away from the end of the database sequence will be ignored by the 
	// PoseSearch. This ensures the search doesn't get stuck at the end of a sequence.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0"))
	float SequenceEndExlusionTime = 0.25f;
};

USTRUCT(BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FMotionMatchingState
{
	GENERATED_BODY()

	// Initializes the minimum required motion matching state
	void InitNewDatabaseSearch(const UPoseSearchDatabase* Database, float SearchThrottleTime);

	// Adds trajectory prediction and history information to ComposedQuery
	void ComposeQuery(const UPoseSearchDatabase* Database, const FTrajectorySampleRange& Trajectory);

	// Internally stores the 'jump' to a new pose/sequence index and asset time for evaluation
	void JumpToPose(const UE::PoseSearch::FDbSearchResult& Result);

	// Updates DbPoseIdx to track DeltaTime and jumps to a follow-up sequence when available
	UE::PoseSearch::FMotionMatchingContinuityParams ComputeContinuityParameters(const FAnimationUpdateContext& Context) const;

	// The current pose we're playing from the database
	UPROPERTY(Transient)
	int32 DbPoseIdx = INDEX_NONE;

	// The current animation we're playing from the database
	UPROPERTY(Transient)
	int32 DbSequenceIdx = INDEX_NONE;

	// The current query feature vector used to search the database for pose candidates
	UPROPERTY(Transient)
	FPoseSearchFeatureVectorBuilder ComposedQuery;

	// Precomputed runtime weights
	UPROPERTY(Transient)
	FPoseSearchWeightsContext WeightsContext;

	// When the database changes, the search parameters are reset
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchDatabase> CurrentDatabase = nullptr;

	// Time since the last pose jump
	UPROPERTY(Transient)
	float ElapsedPoseJumpTime = 0.f;

	// Current time within the asset player node
	UPROPERTY(Transient)
	float AssetPlayerTime = 0.f;

	// Evaluation flags relevant to the state of motion matching
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=State)
	EMotionMatchingFlags Flags = EMotionMatchingFlags::None;
};

/**
* Implementation of the core motion matching algorithm
*
* @param UpdateContext				Input animation update context providing access to the proxy and delta time
* @param Database					Input collection of animations for motion matching
* @param Trajectory					Input motion trajectory samples for pose search queries
* @param Settings					Input motion matching algorithm configuration settings
* @param InOutMotionMatchingState	Input/Output encapsulated motion matching algorithm and state
* @param bOutJumpedToPose			Output motion matching decision to 'jump' to a new pose/sequence index in the database
*/
POSESEARCH_API void UpdateMotionMatchingState(const FAnimationUpdateContext& Context
	, const UPoseSearchDatabase* Database
	, const FTrajectorySampleRange& Trajectory
	, const FMotionMatchingSettings& Settings
	, FMotionMatchingState& InOutMotionMatchingState
);

// Exposes common pose search operations to Blueprint and native code
UCLASS(Category="Animation|Pose Search")
class POSESEARCH_API UPoseSearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Encapsulates and computes the motion matching algorithm for a sequence player
	UFUNCTION(BlueprintPure, Category="Pose Search", meta=(BlueprintThreadSafe, AutoCreateRefTerm="Trajectory, Settings"))
	static void UpdateMotionMatchingForSequencePlayer(const FAnimUpdateContext& AnimUpdateContext
		, const FSequencePlayerReference& SequencePlayer
		, const UPoseSearchDatabase* Database
		, const FTrajectorySampleRange& Trajectory
		, const FMotionMatchingSettings& Settings
		, UPARAM(ref) FMotionMatchingState& InOutMotionMatchingState
	);
};