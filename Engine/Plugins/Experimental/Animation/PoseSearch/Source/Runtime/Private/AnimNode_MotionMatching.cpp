// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "AnimNode_MotionMatching"

/////////////////////////////////////////////////////
// FAnimNode_MotionMatching

void FAnimNode_MotionMatching::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);

	if (IsValidForSearch())
	{
		Query.Reset();
		Query.SetNumZeroed(Database->Schema->Layout.NumFloats);
		QueryBuilder.Init(&Database->Schema->Layout, Query);

		// Set initial pose features
		UE::PoseSearch::IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<UE::PoseSearch::IPoseHistoryProvider>();
		if (PoseHistoryProvider)
		{
			UE::PoseSearch::FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
			QueryBuilder.SetPoseFeatures(Database->Schema, &History);
		}
	}

	DbPoseIdx = INDEX_NONE;

	Source.SetLinkNode(&SequencePlayerNode);
	Source.Initialize(Context);
}

void FAnimNode_MotionMatching::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);
}

void FAnimNode_MotionMatching::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);
	// Note: What if the input database changes? That's not being handled at all!

	if (IsValidForSearch())
	{
		float AssetTime = SequencePlayerNode.GetAccumulatedTime();

		// Step the pose forward
		if (DbPoseIdx != INDEX_NONE)
		{
			check(DbSequenceIdx != INDEX_NONE);

			// Determine roughly which pose we're playing from the database based on the sequence player time
			DbPoseIdx = Database->GetPoseIndexFromAssetTime(DbSequenceIdx, AssetTime);

			// Overwrite query feature vector with the stepped pose from the database
			if (DbPoseIdx != INDEX_NONE)
			{
				TArrayView<const float> PoseFeatureVector = Database->SearchIndex.GetPoseValues(DbPoseIdx);
				QueryBuilder.Copy(PoseFeatureVector);
			}
		}

		// Update trajectory features in the query with the latest inputs
		SetTrajectoryFeatures();

		// Determine how much the updated query vector deviates from the current pose vector
		float CurrentDissimilarity = MAX_flt;
		if (DbPoseIdx != INDEX_NONE)
		{
			CurrentDissimilarity = UE::PoseSearch::ComparePoses(Database->SearchIndex, DbPoseIdx, Query);
		}

		// Search the database for the nearest match to the updated query vector
		UE::PoseSearch::FDbSearchResult Result = UE::PoseSearch::Search(Database, MakeArrayView(Query));
		if (Result.IsValid())
		{
			const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[Result.DbSequenceIdx];

			// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
			bool bBetterPose = Result.Dissimilarity < CurrentDissimilarity;

			// We'll ignore the candidate pose if it is too near to our current pose
			bool bNearbyPose = false;
			if (DbSequenceIdx == Result.DbSequenceIdx)
			{
				bNearbyPose = FMath::Abs(AssetTime - Result.TimeOffsetSeconds) < PoseJumpThreshold;
				if (!bNearbyPose && ResultDbSequence.bLoopAnimation)
				{
					bNearbyPose = FMath::Abs(SequencePlayerNode.GetCurrentAssetLength() - AssetTime - Result.TimeOffsetSeconds) < PoseJumpThreshold;
				}
			}

			// And we won't bother to jump to another pose within the same looping sequence we're already playing
			// (This should probably should be configurable)
			bool bSameCycle = (DbSequenceIdx == Result.DbSequenceIdx) && ResultDbSequence.bLoopAnimation;

			// Start playback from the candidate pose if we determined it was a better option
			if (bBetterPose && !bNearbyPose && !bSameCycle)
			{
				JumpToPose(Context, Result);
			}
		}
	}

	Source.Update(Context);
}

bool FAnimNode_MotionMatching::HasPreUpdate() const
{
#if WITH_EDITORONLY_DATA
	return true;
#else
	return false;
#endif
}

void FAnimNode_MotionMatching::PreUpdate(const UAnimInstance* InAnimInstance)
{
#if WITH_EDITORONLY_DATA
	if (bDebugDraw)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = InAnimInstance->GetSkelMeshComponent();
		check(SkeletalMeshComponent);

		UE::PoseSearch::FDebugDrawParams DrawParams;
		DrawParams.RootTransform = SkeletalMeshComponent->GetBoneTransform(0);
		DrawParams.Flags = UE::PoseSearch::EDebugDrawFlags::DrawQuery;
		DrawParams.Database = Database;
		DrawParams.Query = Query;
		DrawParams.World = SkeletalMeshComponent->GetWorld();
		DrawParams.DefaultLifeTime = 0.0f;
		UE::PoseSearch::Draw(DrawParams);
	}
#endif
}

void FAnimNode_MotionMatching::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

bool FAnimNode_MotionMatching::IsValidForSearch() const
{
	return Database && Database->IsValidForSearch();
}

void FAnimNode_MotionMatching::SetTrajectoryFeatures()
{
	// This is all placeholder-- trajectory should be provided externally rather than computed in this node.
	// And we shouldn't be assuming the format of the database's schema!

	// Add instantaneous root velocity to query
	FPoseSearchFeatureDesc TrajectoryFeature;
	TrajectoryFeature.Domain = EPoseSearchFeatureDomain::Time;
	TrajectoryFeature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
	TrajectoryFeature.SubsampleIdx = 0;
	TrajectoryFeature.Type = EPoseSearchFeatureType::LinearVelocity;

	// I'm sure there is probably a less magical way to get LocalVelocity into the same space as the animation data...
	FQuat Rotate(FVector::ZAxisVector, HALF_PI);

	// Apply the updated trajectory to the query feature vector, leaving any existing pose information intact
	QueryBuilder.SetVector(TrajectoryFeature, Rotate.RotateVector(LocalVelocity));
}

void FAnimNode_MotionMatching::JumpToPose(const FAnimationUpdateContext& Context, UE::PoseSearch::FDbSearchResult Result)
{
	// Remember which pose and sequence we're playing from the database
	DbPoseIdx = Result.PoseIdx;
	DbSequenceIdx = Result.DbSequenceIdx;

	// Immediately jump to the pose by updating the embedded sequence player node
	const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[Result.DbSequenceIdx];
	SequencePlayerNode.Sequence = ResultDbSequence.Sequence;
	SequencePlayerNode.SetAccumulatedTime(Result.TimeOffsetSeconds);
	SequencePlayerNode.bLoopAnimation = ResultDbSequence.bLoopAnimation;

	// Use inertial blending to smooth over the transition
	// It would be cool in the future to adjust the blend time by amount of dissimilarity, but we'll need a standardized distance metric first.
	if (BlendTime > 0.0f)
	{
		UE::Anim::IInertializationRequester* InertializationRequester = Context.GetMessage<UE::Anim::IInertializationRequester>();
		if (InertializationRequester)
		{
			InertializationRequester->RequestInertialization(BlendTime);
		}
	}
}

#undef LOCTEXT_NAMESPACE