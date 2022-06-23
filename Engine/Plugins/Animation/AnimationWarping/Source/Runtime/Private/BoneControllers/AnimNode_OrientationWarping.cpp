// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimRootMotionProvider.h"

DECLARE_CYCLE_STAT(TEXT("OrientationWarping Eval"), STAT_OrientationWarping_Eval, STATGROUP_Anim);

#if ENABLE_ANIM_DEBUG
static TAutoConsoleVariable<int32> CVarAnimNodeOrientationWarpingDebug(TEXT("a.AnimNode.OrientationWarping.Debug"), 0, TEXT("Turn on visualization debugging for Orientation Warping"));
static TAutoConsoleVariable<int32> CVarAnimNodeOrientationWarpingVerbose(TEXT("a.AnimNode.OrientationWarping.Verbose"), 0, TEXT("Turn on verbose graph debugging for Orientation Warping"));
static TAutoConsoleVariable<int32> CVarAnimNodeOrientationWarpingEnable(TEXT("a.AnimNode.OrientationWarping.Enable"), 1, TEXT("Toggle Orientation Warping"));
#endif

namespace UE::Anim
{
	static inline FVector GetAxisVector(const EAxis::Type& InAxis)
	{
		switch (InAxis)
		{
		case EAxis::X:
			return FVector::ForwardVector;
		case EAxis::Y:
			return FVector::RightVector;
		default:
			return FVector::UpVector;
		};
	}

	static inline bool IsInvalidWarpingAngleDegrees(float Angle, float Tolerance)
	{
		Angle = FRotator::NormalizeAxis(Angle);
		return FMath::IsNearlyZero(Angle, Tolerance) || FMath::IsNearlyEqual(FMath::Abs(Angle), 180.f, Tolerance);
	}
}

void FAnimNode_OrientationWarping::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeOrientationWarpingVerbose.GetValueOnAnyThread() == 1)
	{
		if (Mode == EWarpingEvaluationMode::Manual)
		{
			DebugLine += TEXT("\n - Evaluation Mode: (Manual)");
			DebugLine += FString::Printf(TEXT("\n - Orientation Angle: (%.3fd)"), FMath::RadiansToDegrees(ActualOrientationAngle));
		}
		else
		{
			DebugLine += TEXT("\n - Evaluation Mode: (Graph)");
			DebugLine += FString::Printf(TEXT("\n - Orientation Angle: (%.3fd)"), FMath::RadiansToDegrees(ActualOrientationAngle));
			DebugLine += FString::Printf(TEXT("\n - Locomotion Angle: (%.3fd)"), FMath::RadiansToDegrees(LocomotionAngle));
			DebugLine += FString::Printf(TEXT("\n - Locomotion Delta Angle Threshold: (%.3fd)"), LocomotionAngleDeltaThreshold);
#if WITH_EDITORONLY_DATA
			DebugLine += FString::Printf(TEXT("\n - Root Motion Delta Attribute Found: %s)"), (bFoundRootMotionAttribute) ? TEXT("true") : TEXT("false"));
#endif
		}
		DebugLine += FString::Printf(TEXT("\n - Distributed Bone Orientation Alpha: (%.3fd)"), DistributedBoneOrientationAlpha);
		if (const UEnum* TypeEnum = FindObject<UEnum>(nullptr, TEXT("/Script/CoreUObject.EAxis")))
		{
			DebugLine += FString::Printf(TEXT("\n - Rotation Axis: (%s)"), *(TypeEnum->GetNameStringByIndex(static_cast<int32>(RotationAxis))));
		}
		DebugLine += FString::Printf(TEXT("\n - Rotation Interpolation Speed: (%.3fd)"), RotationInterpSpeed);
	}
	else
#endif
	{
	DebugLine += FString::Printf(TEXT("(Orientation Angle: %.3fd)"), FMath::RadiansToDegrees(ActualOrientationAngle));
	}
	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_OrientationWarping::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);

	PreviousRootMotionDeltaDirection = FVector::ZeroVector;
	PreviousOrientationAngle = 0.f;
	ActualOrientationAngle = 0.f;
}

void FAnimNode_OrientationWarping::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
}

void FAnimNode_OrientationWarping::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_OrientationWarping_Eval);
	check(OutBoneTransforms.Num() == 0);

	ActualOrientationAngle = OrientationAngle;

	const FVector RotationAxisVector = UE::Anim::GetAxisVector(RotationAxis);
	FVector RootMotionDeltaDirection = FVector::ZeroVector;
	FVector LocomotionForward = FVector::ZeroVector;

	bool bGraphDrivenWarping = false;
	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

	if (Mode == EWarpingEvaluationMode::Graph)
	{
		bGraphDrivenWarping = !!RootMotionProvider;
		ensureMsgf(bGraphDrivenWarping, TEXT("Graph driven Orientation Warping expected a valid root motion delta provider interface."));
	}

#if WITH_EDITORONLY_DATA
	bFoundRootMotionAttribute = false;
#endif

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

	if (bGraphDrivenWarping)
	{
		FTransform RootMotionTransformDelta = FTransform::Identity;
		bGraphDrivenWarping = RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, RootMotionTransformDelta);

		// Graph driven orientation warping will modify the incoming root motion to orient towards the intended locomotion angle
		if (bGraphDrivenWarping)
		{
#if WITH_EDITORONLY_DATA
			// Graph driven Orientation Warping expects a root motion delta to be present in the attribute stream.
			bFoundRootMotionAttribute = true;
#endif

			// In UE, forward is defined as +x; consequently this is also true when sampling an actor's velocity. Historically the skeletal 
			// mesh component forward will not match the actor, requiring us to correct the rotation before sampling the LocomotionForward.
			// In order to make orientation warping 'pure' in the future we will need to provide more context about the intent of
			// the actor vs the intent of the animation in their respective spaces. Specifically, we will need some form the following information:
			//
			// 1. Actor Forward
			// 2. Actor Velocity
			// 3. Skeletal Mesh Relative Rotation

			LocomotionAngle = FRotator::NormalizeAxis(LocomotionAngle);
			LocomotionAngle = FMath::DegreesToRadians(LocomotionAngle);
			const FQuat LocomotionRotation = FQuat(RotationAxisVector, LocomotionAngle);

			const FTransform SkeletalMeshRelativeTransform = Output.AnimInstanceProxy->GetComponentRelativeTransform();
			const FQuat SkeletalMeshRelativeRotation = SkeletalMeshRelativeTransform.GetRotation();
			LocomotionForward = SkeletalMeshRelativeRotation.UnrotateVector(LocomotionRotation.GetForwardVector()).GetSafeNormal();

			const FVector RootMotionDeltaTranslation = RootMotionTransformDelta.GetTranslation();
			RootMotionDeltaDirection = RootMotionDeltaTranslation.GetSafeNormal();

			// Capture the delta rotation from the axis of motion we care about
			FQuat WarpedRotation = FQuat::FindBetween(RootMotionDeltaDirection, LocomotionForward);
			ActualOrientationAngle = WarpedRotation.GetTwistAngle(RotationAxisVector);

			// Motion Matching may return an animation that deviates a lot from the movement direction (e.g movement direction going bwd and motion matching could return the fwd animation for a few frames)
			// When that happens, since we use the delta between root motion and movement direction, we would be over-rotating the lower body and breaking the pose during those frames
			// So, when that happens we use the inverse of the movement direction to calculate our target rotation. 
			// This feels a bit 'hacky' but its the only option I've found so far to mitigate the problem
			if (LocomotionAngleDeltaThreshold > 0.f && FMath::Abs(FMath::RadiansToDegrees(ActualOrientationAngle)) > LocomotionAngleDeltaThreshold)
			{
				WarpedRotation = FQuat::FindBetween(RootMotionDeltaDirection, -LocomotionForward);
				ActualOrientationAngle = WarpedRotation.GetTwistAngle(RotationAxisVector);
			}

			// For interpolated warping, guarantee that PreviousOrientationAngle is respect to the current frame's root motion direction 
			float RootMotionDeltaAngleDifference = FMath::Acos(RootMotionDeltaDirection.Dot(PreviousRootMotionDeltaDirection));
			RootMotionDeltaAngleDifference *= FMath::Sign(RotationAxisVector.Dot(RootMotionDeltaDirection.Cross(PreviousRootMotionDeltaDirection)));

			PreviousRootMotionDeltaDirection = RootMotionDeltaDirection;
			PreviousOrientationAngle += RootMotionDeltaAngleDifference;

			// Rotate the root motion delta fully by the warped angle
			const FVector WarpedRootMotionTranslationDelta = WarpedRotation.RotateVector(RootMotionDeltaTranslation);
			RootMotionTransformDelta.SetTranslation(WarpedRootMotionTranslationDelta);

			// Forward the side effects of orientation warping on the root motion contribution for this sub-graph
			const bool bRootMotionOverridden = RootMotionProvider->OverrideRootMotion(RootMotionTransformDelta, Output.CustomAttributes);
			ensureMsgf(bRootMotionOverridden, TEXT("Graph driven Orientation Warping expected a root motion delta to be present in the attribute stream prior to warping/overriding it."));
		}
		else
		{
			// Early exit on missing root motion delta attribute
			return;
		}
	} 
	else
	{
		// Manual orientation warping will take the angle directly
		ActualOrientationAngle = FRotator::NormalizeAxis(ActualOrientationAngle);
		ActualOrientationAngle = FMath::DegreesToRadians(ActualOrientationAngle);
	}

	// Optionally interpolate the effective orientation towards the target orientation angle
	if (RotationInterpSpeed > 0.f)
	{
		ActualOrientationAngle = FMath::FInterpTo(PreviousOrientationAngle, ActualOrientationAngle, Output.AnimInstanceProxy->GetDeltaSeconds(), RotationInterpSpeed);
		PreviousOrientationAngle = ActualOrientationAngle;
	}

	// Allow the alpha value of the node to affect the final rotation
	ActualOrientationAngle *= ActualAlpha;

#if ENABLE_ANIM_DEBUG
	bool bDebugging = false;
#if WITH_EDITORONLY_DATA
	bDebugging = bDebugging || bEnableDebugDraw;
#else
	constexpr float DebugDrawScale = 1.f;
#endif
	bDebugging = bDebugging || CVarAnimNodeOrientationWarpingDebug.GetValueOnAnyThread() == 1;

	if (bDebugging)
	{
		const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();
		const FVector ActorForwardDirection = Output.AnimInstanceProxy->GetActorTransform().GetRotation().GetForwardVector();

		const FVector ForwardDirection = bGraphDrivenWarping
			? ComponentTransform.GetRotation().RotateVector(LocomotionForward)
			: ActorForwardDirection;

		FVector DebugArrowOffset = FVector::ZAxisVector * DebugDrawScale;
		Output.AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ComponentTransform.GetLocation() + DebugArrowOffset,
			ComponentTransform.GetLocation() + DebugArrowOffset + ForwardDirection * 100.f * DebugDrawScale,
			40.f * DebugDrawScale, FColor::Red, false, 0.f, 2.f * DebugDrawScale);

		const FVector RotationDirection = bGraphDrivenWarping
			? ComponentTransform.GetRotation().RotateVector(RootMotionDeltaDirection)
			: ActorForwardDirection.RotateAngleAxis(OrientationAngle, RotationAxisVector);

		DebugArrowOffset += DebugArrowOffset;
		Output.AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ComponentTransform.GetLocation() + DebugArrowOffset,
			ComponentTransform.GetLocation() + DebugArrowOffset + RotationDirection * 100.f * DebugDrawScale,
			40.f * DebugDrawScale, FColor::Blue, false, 0.f, 2.f * DebugDrawScale);

		const float ActualOrientationAngleDegrees = FMath::RadiansToDegrees(ActualOrientationAngle);
		const FVector WarpedRotationDirection = bGraphDrivenWarping 
			? RotationDirection.RotateAngleAxis(ActualOrientationAngleDegrees, RotationAxisVector)
			: ActorForwardDirection.RotateAngleAxis(ActualOrientationAngleDegrees, RotationAxisVector);

		DebugArrowOffset += DebugArrowOffset;
		Output.AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ComponentTransform.GetLocation() + DebugArrowOffset,
			ComponentTransform.GetLocation() + DebugArrowOffset + WarpedRotationDirection * 100.f * DebugDrawScale,
			40.f * DebugDrawScale, FColor::Green, false, 0.f, 2.f * DebugDrawScale);
	}
#endif
		
	// Rotate Root Bone first, as that cheaply rotates the whole pose with one transformation.
	if (!FMath::IsNearlyZero(DistributedBoneOrientationAlpha, KINDA_SMALL_NUMBER))
	{
		const FQuat RootRotation = FQuat(RotationAxisVector, ActualOrientationAngle * DistributedBoneOrientationAlpha);
		const FCompactPoseBoneIndex RootBoneIndex(0);

		FTransform RootBoneTransform(Output.Pose.GetComponentSpaceTransform(RootBoneIndex));
		RootBoneTransform.SetRotation(RootRotation * RootBoneTransform.GetRotation());
		RootBoneTransform.NormalizeRotation();
		Output.Pose.SetComponentSpaceTransform(RootBoneIndex, RootBoneTransform);
	}

	const int32 NumSpineBones = SpineBoneDataArray.Num();
	const bool bSpineOrientationAlpha = !FMath::IsNearlyZero(DistributedBoneOrientationAlpha, KINDA_SMALL_NUMBER);
	const bool bUpdateSpineBones = (NumSpineBones > 0) && bSpineOrientationAlpha;

	if (bUpdateSpineBones)
	{
		// Spine bones counter rotate body orientation evenly across all bones.
		for (int32 ArrayIndex = 0; ArrayIndex < NumSpineBones; ArrayIndex++)
		{
			const FOrientationWarpingSpineBoneData& BoneData = SpineBoneDataArray[ArrayIndex];
			const FQuat SpineBoneCounterRotation = FQuat(RotationAxisVector, -ActualOrientationAngle * DistributedBoneOrientationAlpha * BoneData.Weight);
			check(BoneData.Weight > 0.f);

			FTransform SpineBoneTransform(Output.Pose.GetComponentSpaceTransform(BoneData.BoneIndex));
			SpineBoneTransform.SetRotation((SpineBoneCounterRotation * SpineBoneTransform.GetRotation()));
			SpineBoneTransform.NormalizeRotation();
			Output.Pose.SetComponentSpaceTransform(BoneData.BoneIndex, SpineBoneTransform);
		}
	}

	const float IKFootRootOrientationAlpha = 1.f - DistributedBoneOrientationAlpha;
	const bool bUpdateIKFootRoot = (IKFootData.IKFootRootBoneIndex != FCompactPoseBoneIndex(INDEX_NONE)) && !FMath::IsNearlyZero(IKFootRootOrientationAlpha, KINDA_SMALL_NUMBER);

	// Rotate IK Foot Root
	if (bUpdateIKFootRoot)
	{
		const FQuat BoneRotation = FQuat(RotationAxisVector, ActualOrientationAngle * IKFootRootOrientationAlpha);

		FTransform IKFootRootTransform(Output.Pose.GetComponentSpaceTransform(IKFootData.IKFootRootBoneIndex));
		IKFootRootTransform.SetRotation(BoneRotation * IKFootRootTransform.GetRotation());
		IKFootRootTransform.NormalizeRotation();
		Output.Pose.SetComponentSpaceTransform(IKFootData.IKFootRootBoneIndex, IKFootRootTransform);

		// IK Feet 
		// These match the root orientation, so don't rotate them. Just preserve root rotation. 
		// We need to update their translation though, since we rotated their parent (the IK Foot Root bone).
		const int32 NumIKFootBones = IKFootData.IKFootBoneIndexArray.Num();
		const bool bUpdateIKFootBones = bUpdateIKFootRoot && (NumIKFootBones > 0);

		if (bUpdateIKFootBones)
		{
			const FQuat IKFootRotation = FQuat(RotationAxisVector, -ActualOrientationAngle * IKFootRootOrientationAlpha);

			for (int32 ArrayIndex = 0; ArrayIndex < NumIKFootBones; ArrayIndex++)
			{
				const FCompactPoseBoneIndex& IKFootBoneIndex = IKFootData.IKFootBoneIndexArray[ArrayIndex];

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
#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeOrientationWarpingEnable.GetValueOnAnyThread() == 0)
	{
		return false;
	}
#endif
	if (RotationAxis == EAxis::None)
	{
		return false;
	}

	if (Mode == EWarpingEvaluationMode::Manual && UE::Anim::IsInvalidWarpingAngleDegrees(OrientationAngle, KINDA_SMALL_NUMBER))
	{
		return false;
	}

	if (SpineBoneDataArray.IsEmpty())
	{
		return false;
	}
	else
	{
		for (const auto& Spine : SpineBoneDataArray)
		{
			if (Spine.BoneIndex == INDEX_NONE)
			{
				return false;
			}
		}
	}

	if (IKFootData.IKFootRootBoneIndex == INDEX_NONE)
	{
		return false;
	}

	if (IKFootData.IKFootBoneIndexArray.IsEmpty())
	{
		return false;
	}
	else
	{
		for (const auto& IKFootBoneIndex : IKFootData.IKFootBoneIndexArray)
		{
			if (IKFootBoneIndex == INDEX_NONE)
			{
				return false;
			}
		}
	}
	return true;
}

void FAnimNode_OrientationWarping::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	IKFootRootBone.Initialize(RequiredBones);
	IKFootData.IKFootRootBoneIndex = IKFootRootBone.GetCompactPoseIndex(RequiredBones);

	IKFootData.IKFootBoneIndexArray.Reset();
	for (auto& Bone : IKFootBones)
	{
		Bone.Initialize(RequiredBones);
		IKFootData.IKFootBoneIndexArray.Add(Bone.GetCompactPoseIndex(RequiredBones));
	}

	SpineBoneDataArray.Reset();
	for (auto& Bone : SpineBones)
	{
		Bone.Initialize(RequiredBones);
		SpineBoneDataArray.Add(FOrientationWarpingSpineBoneData(Bone.GetCompactPoseIndex(RequiredBones)));
	}

	if (SpineBoneDataArray.Num() > 0)
	{
		// Sort bones indices so we can transform parent before child
		SpineBoneDataArray.Sort(FOrientationWarpingSpineBoneData::FCompareBoneIndex());

		// Assign Weights.
		TArray<int32, TInlineAllocator<20>> IndicesToUpdate;

		for (int32 Index = SpineBoneDataArray.Num() - 1; Index >= 0; Index--)
		{
			// If this bone's weight hasn't been updated, scan its parents.
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