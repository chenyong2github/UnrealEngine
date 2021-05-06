// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "Animation/AnimRootMotionProvider.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"

DECLARE_CYCLE_STAT(TEXT("OrientationWarping Eval"), STAT_OrientationWarping_Eval, STATGROUP_Anim);

FAnimNode_OrientationWarping::FAnimNode_OrientationWarping()
	: Mode(EWarpingEvaluationMode::Manual)
	, LocomotionAngle(0.f)
	, IKFootRootBoneIndex(INDEX_NONE)
{
}

static inline FVector GetAxisVector(const EAxis::Type& InAxis)
{
	switch (InAxis)
	{
	case EAxis::X: return FVector::ForwardVector;
	case EAxis::Y: return FVector::RightVector;
	default:
	case EAxis::Z: return FVector::UpVector;
	};
}

static inline bool IsInvalidWarpingAngle(float Angle, float Tolerance)
{
	return FMath::IsNearlyZero(Angle, Tolerance) || FMath::IsNearlyEqual(FMath::Abs(Angle), PI, Tolerance);
}

void FAnimNode_OrientationWarping::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
 	DebugLine += FString::Printf(TEXT("Angle(%.1fd)"), LocomotionAngle);

	DebugData.AddDebugItem(DebugLine);

	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_OrientationWarping::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
}

void FAnimNode_OrientationWarping::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
}

void FAnimNode_OrientationWarping::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_OrientationWarping_Eval);

	check(OutBoneTransforms.Num() == 0);

	// We will likely need to revisit LocomotionAngle participating as an input to orientation warping.
	// Without velocity information from the motion model (such as the capsule), LocomotionAngle isn't enough
	// information in isolation for all cases when deciding to warp.
	//
	// For example imagine that the motion model has stopped moving with zero velocity due to a
	// transition into a strafing stop. During that transition we may play an animation with non-zero 
	// velocity for an arbitrary number of frames. In this scenario the concept of direction is meaningless 
	// since we cannot orient the animation to match a zero velocity and consequently a zero direction, 
	// since that would break the pose. For those frames, we would incorrectly over-orient the strafe.
	//
	// The solution may be instead to pass velocity with the actor base rotation, allowing us to retain
	// speed information about the motion. It may also allow us to do more complex orienting behavior 
	// when multiple degrees of freedom can be considered.
	float LocomotionRotationAngle = FMath::DegreesToRadians(FRotator::NormalizeAxis(LocomotionAngle));

	const FVector LocomotionRotationAxis = GetAxisVector(Settings.YawRotationAxis);
	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();
	const bool bGraphDrivenWarping = RootMotionProvider && Mode == EWarpingEvaluationMode::Graph;

	// Graph driven orientation warping will modify the incoming root motion to orient towards the intended locomotion angle
	if (bGraphDrivenWarping)
	{
		if (IsInvalidWarpingAngle(LocomotionRotationAngle, KINDA_SMALL_NUMBER))
		{
			return;
		}

		FTransform RootMotionTransformDelta;
		const bool bRootMotionDeltaPresent = RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, RootMotionTransformDelta);

		if (ensure(bRootMotionDeltaPresent))
		{
			// In UE, forward is defined as +x; consequently this is also true when sampling an actor's velocity. Sometimes the skeletal 
			// mesh component forward will not match the actor, requiring us to correct the rotation before sampling the LocomotionForward.
			// In order to make orientation warping 'pure' in the future we will need to provide more context about the intent of
			// the actor vs the intent of the animation in their respective spaces. Specifically, we will need some form the following information:
			//
			// 1. Actor Forward
			// 2. Actor Velocity
			// 3. Skeletal Mesh Relative Rotation
			const FTransform SkeletalMeshRelativeTransform = Output.AnimInstanceProxy->GetComponentRelativeTransform();
			const FQuat SkeletalMeshRelativeRotation = SkeletalMeshRelativeTransform.GetRotation();
			const FQuat LocomotionRotation = FQuat(LocomotionRotationAxis, LocomotionRotationAngle);
			const FVector LocomotionForwardDir = SkeletalMeshRelativeRotation.UnrotateVector(LocomotionRotation.GetForwardVector());
			const FVector RootMotionDeltaDir = RootMotionTransformDelta.GetTranslation();

			// Capture the delta rotation from the axis of motion we care about
			const FQuat WarpedRotation = FQuat::FindBetween(RootMotionDeltaDir, LocomotionForwardDir);
			LocomotionRotationAngle = WarpedRotation.GetTwistAngle(LocomotionRotationAxis);

			if (IsInvalidWarpingAngle(LocomotionRotationAngle, KINDA_SMALL_NUMBER))
			{
				return;
			}

			// Rotate the root motion delta fully by the warped angle
			const FVector RootMotionTranslationDelta = RootMotionTransformDelta.GetTranslation();
			const FVector WarpedRootMotionTranslationDelta = WarpedRotation.RotateVector(RootMotionTranslationDelta);
			RootMotionTransformDelta.SetTranslation(WarpedRootMotionTranslationDelta);

			// Forward the side effects of orientation warping on the root motion contribution for this sub-graph
			const bool bRootMotionOverridden = RootMotionProvider->OverrideRootMotion(RootMotionTransformDelta, Output.CustomAttributes);
			ensure(bRootMotionOverridden);
		}
	} 
	else if (IsInvalidWarpingAngle(LocomotionRotationAngle, KINDA_SMALL_NUMBER))
	{
		return;
	}

	const float BodyOrientationAlpha = FMath::Clamp(Settings.BodyOrientationAlpha, 0.f, 1.f);

	// Rotate Root Bone first, as that cheaply rotates the whole pose with one transformation.
	if (!FMath::IsNearlyZero(BodyOrientationAlpha, KINDA_SMALL_NUMBER))
	{
		const FQuat RootRotation = FQuat(LocomotionRotationAxis, LocomotionRotationAngle * BodyOrientationAlpha);
		const FCompactPoseBoneIndex RootBoneIndex(0);

		FTransform RootBoneTransform(Output.Pose.GetComponentSpaceTransform(RootBoneIndex));
		RootBoneTransform.SetRotation(RootRotation * RootBoneTransform.GetRotation());
		RootBoneTransform.NormalizeRotation();
		Output.Pose.SetComponentSpaceTransform(RootBoneIndex, RootBoneTransform);
	}
		
	const int32 NumSpineBones = SpineBoneDataArray.Num();
	const bool bBodyOrientationAlpha = !FMath::IsNearlyZero(BodyOrientationAlpha, KINDA_SMALL_NUMBER);
	const bool bUpdateSpineBones = (NumSpineBones > 0) && bBodyOrientationAlpha;

	if (bUpdateSpineBones)
	{
		// Spine bones counter rotate body orientation evenly across all bones.
		for (int32 ArrayIndex = 0; ArrayIndex < NumSpineBones; ArrayIndex++)
		{
			const FOrientationWarpingSpineBoneData& BoneData = SpineBoneDataArray[ArrayIndex];
			const FQuat SpineBoneCounterRotation = FQuat(LocomotionRotationAxis, -LocomotionRotationAngle * BodyOrientationAlpha * BoneData.Weight);
			check(BoneData.Weight > 0.f);

			FTransform SpineBoneTransform(Output.Pose.GetComponentSpaceTransform(BoneData.BoneIndex));
			SpineBoneTransform.SetRotation((SpineBoneCounterRotation * SpineBoneTransform.GetRotation()));
			SpineBoneTransform.NormalizeRotation();
			Output.Pose.SetComponentSpaceTransform(BoneData.BoneIndex, SpineBoneTransform);
		}
	}

	const float IKFootRootOrientationAlpha = 1.f - BodyOrientationAlpha;
	const bool bUpdateIKFootRoot = (IKFootRootBoneIndex != FCompactPoseBoneIndex(INDEX_NONE)) && !FMath::IsNearlyZero(IKFootRootOrientationAlpha, KINDA_SMALL_NUMBER);

	// Rotate IK Foot Root
	if (bUpdateIKFootRoot)
	{
		const FQuat BoneRotation = FQuat(LocomotionRotationAxis, LocomotionRotationAngle * IKFootRootOrientationAlpha);

		FTransform IKFootRootTransform(Output.Pose.GetComponentSpaceTransform(IKFootRootBoneIndex));
		IKFootRootTransform.SetRotation(BoneRotation * IKFootRootTransform.GetRotation());
		IKFootRootTransform.NormalizeRotation();
		Output.Pose.SetComponentSpaceTransform(IKFootRootBoneIndex, IKFootRootTransform);

		// IK Feet 
		// These match the root orientation, so don't rotate them. Just preserve root rotation. 
		// We need to update their translation though, since we rotated their parent (the IK Foot Root bone).
		const int32 NumIKFootBones = IKFootBoneIndexArray.Num();
		const bool bUpdateIKFootBones = bUpdateIKFootRoot && (NumIKFootBones > 0);

		if (bUpdateIKFootBones)
		{
			const FQuat IKFootRotation = FQuat(LocomotionRotationAxis, -LocomotionRotationAngle * IKFootRootOrientationAlpha);

			for (int32 ArrayIndex = 0; ArrayIndex < NumIKFootBones; ArrayIndex++)
			{
				const FCompactPoseBoneIndex& IKFootBoneIndex = IKFootBoneIndexArray[ArrayIndex];

				FTransform IKFootBoneTransform(Output.Pose.GetComponentSpaceTransform(IKFootBoneIndex));
				IKFootBoneTransform.SetRotation(IKFootRotation * IKFootBoneTransform.GetRotation());
				IKFootBoneTransform.NormalizeRotation();
				Output.Pose.SetComponentSpaceTransform(IKFootBoneIndex, IKFootBoneTransform);
			}
		}
	}

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

bool FAnimNode_OrientationWarping::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	bool bIKFootRootIsValid = IKFootRootBoneIndex != INDEX_NONE;
	bool bIKFeetAreValid = IKFootBoneIndexArray.Num() > 0;
	for (const auto& IKFootBoneIndex : IKFootBoneIndexArray)
	{
		bIKFeetAreValid = bIKFeetAreValid && IKFootBoneIndex != INDEX_NONE;
	}

	bool bSpineIsValid = SpineBoneDataArray.Num() > 0;
	for (const auto& Spine : SpineBoneDataArray)
	{
		bSpineIsValid = bSpineIsValid && Spine.BoneIndex != INDEX_NONE;
	}

	return bIKFootRootIsValid && bIKFeetAreValid && bSpineIsValid;
}

void FAnimNode_OrientationWarping::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Settings.IKFootRootBone.Initialize(RequiredBones);
	IKFootRootBoneIndex = Settings.IKFootRootBone.GetCompactPoseIndex(RequiredBones);

	{
		IKFootBoneIndexArray.Reset();
		for (auto& BoneSettings : Settings.IKFootBones)
		{
			BoneSettings.Initialize(RequiredBones);
			IKFootBoneIndexArray.Add(BoneSettings.GetCompactPoseIndex(RequiredBones));
		}
	}

	{
		SpineBoneDataArray.Reset();
		for (auto& BoneSettings : Settings.SpineBones)
		{
			BoneSettings.Bone.Initialize(RequiredBones);
			SpineBoneDataArray.Add(FOrientationWarpingSpineBoneData(BoneSettings.Bone.GetCompactPoseIndex(RequiredBones)));
		}

		if (SpineBoneDataArray.Num() > 0)
		{
			// Sort bones indices so we can transform parent before child
			SpineBoneDataArray.Sort(FOrientationWarpingSpineBoneData::FCompareBoneIndex());

			// Assign Weights.
			{
				TArray<int32> IndicesToUpdate;

				for (int32 Index = SpineBoneDataArray.Num() - 1; Index >= 0; Index--)
				{
					// If this bone's weight hasn't been updated, scan his parents.
					// If parents have weight, we add it to 'ExistingWeight'.
					// split (1.f - 'ExistingWeight') between all members of the chain that have no weight yet.
					if (SpineBoneDataArray[Index].Weight == 0.f)
					{
						IndicesToUpdate.Reset(SpineBoneDataArray.Num());
						float ExistingWeight = 0.f;
						IndicesToUpdate.Add(Index);

						const FCompactPoseBoneIndex CompactBoneIndex = SpineBoneDataArray[Index].BoneIndex;
						for (int32 ParentIndex = Index - 1; ParentIndex >= 0; ParentIndex--)
						{
							if (RequiredBones.BoneIsChildOf(CompactBoneIndex, SpineBoneDataArray[ParentIndex].BoneIndex))
							{
								if (SpineBoneDataArray[ParentIndex].Weight > 0.f)
								{
									ExistingWeight += SpineBoneDataArray[ParentIndex].Weight;
								}
								else
								{
									IndicesToUpdate.Add(ParentIndex);
								}
							}
						}

						check(IndicesToUpdate.Num() > 0);
						const float WeightToShare = 1.f - ExistingWeight;
						const float IndividualWeight = WeightToShare / float(IndicesToUpdate.Num());

						for (int32 UpdateListIndex = 0; UpdateListIndex < IndicesToUpdate.Num(); UpdateListIndex++)
						{
							SpineBoneDataArray[IndicesToUpdate[UpdateListIndex]].Weight = IndividualWeight;
						}
					}
				}
			}
		}
	}
}