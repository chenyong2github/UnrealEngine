// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"


/////////////////////////////////////////////////////
// FFortAnimNode_OrientationWarping

DECLARE_CYCLE_STAT(TEXT("OrientationWarping Eval"), STAT_OrientationWarping_Eval, STATGROUP_Anim);


FAnimNode_OrientationWarping::FAnimNode_OrientationWarping()
	: LocomotionAngle(0.f)
	, IKFootRootBoneIndex(INDEX_NONE)
	, CachedDeltaTime(0.f)
{
}

static FVector GetAxisVector(const EAxis::Type& InAxis)
{
	switch (InAxis)
	{
	case EAxis::X: return FVector::ForwardVector;
	case EAxis::Y: return FVector::RightVector;
	default:
	case EAxis::Z: return FVector::UpVector;
	};
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

	CachedDeltaTime += Context.GetDeltaTime();
}

void FAnimNode_OrientationWarping::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_OrientationWarping_Eval);

	check(OutBoneTransforms.Num() == 0);
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	const float YawAngleRadians = FMath::DegreesToRadians(FRotator::NormalizeAxis(LocomotionAngle));
	if (!FMath::IsNearlyZero(YawAngleRadians, KINDA_SMALL_NUMBER))
	{
		const FVector RotationAxis = GetAxisVector(Settings.YawRotationAxis);
		const float BodyOrientationAlpha = FMath::Clamp(Settings.BodyOrientationAlpha, 0.f, 1.f);
		const float IKFootRootOrientationAlpha = 1.f - BodyOrientationAlpha;

		// Rotate Root Bone first, as that cheaply rotates the whole pose with one transformation.
		if (!FMath::IsNearlyZero(BodyOrientationAlpha, KINDA_SMALL_NUMBER))
		{
			const FQuat RootRotation = FQuat(RotationAxis, YawAngleRadians * BodyOrientationAlpha);

			const FCompactPoseBoneIndex RootBoneIndex(0);
			FTransform RootBoneTransform(Output.Pose.GetComponentSpaceTransform(RootBoneIndex));
			RootBoneTransform.SetRotation(RootRotation * RootBoneTransform.GetRotation());
			RootBoneTransform.NormalizeRotation();
			Output.Pose.SetComponentSpaceTransform(RootBoneIndex, RootBoneTransform);
		}
		
		const int32 NumSpineBones = SpineBoneDataArray.Num();
		const int32 NumIKFootBones = IKFootBoneIndexArray.Num();

		const bool bUpdateSpineBones = (NumSpineBones > 0) && !FMath::IsNearlyZero(BodyOrientationAlpha, KINDA_SMALL_NUMBER);
		const bool bUpdateIKFootRoot = (IKFootRootBoneIndex != FCompactPoseBoneIndex(INDEX_NONE)) && !FMath::IsNearlyZero(IKFootRootOrientationAlpha, KINDA_SMALL_NUMBER);
		const bool bUpdateIKFootBones = bUpdateIKFootRoot && (NumIKFootBones > 0);

		if (bUpdateSpineBones || bUpdateIKFootRoot || bUpdateIKFootBones)
		{
			if (bUpdateSpineBones)
			{
				// Spine bones counter rotate body orientation evenly across all bones.
				for (int32 ArrayIndex = 0; ArrayIndex < NumSpineBones; ArrayIndex++)
				{
					const FOrientationWarpingSpineBoneData& BoneData = SpineBoneDataArray[ArrayIndex];

					const FQuat SpineBoneCounterRotation = FQuat(RotationAxis, -YawAngleRadians * BodyOrientationAlpha * BoneData.Weight);
					check(BoneData.Weight > 0.f);

					//FTransform SpineBoneTransform(Output.Pose.GetLocalSpaceTransform(BoneData.BoneIndex));
					FTransform SpineBoneTransform(Output.Pose.GetComponentSpaceTransform(BoneData.BoneIndex));
					SpineBoneTransform.SetRotation(SpineBoneCounterRotation * SpineBoneTransform.GetRotation());
					SpineBoneTransform.NormalizeRotation();
					Output.Pose.SetComponentSpaceTransform(BoneData.BoneIndex, SpineBoneTransform);
				}
			}

			// Rotate IK Foot Root
			if (bUpdateIKFootRoot)
			{
				const FQuat BoneRotation = FQuat(RotationAxis, YawAngleRadians * IKFootRootOrientationAlpha);

				FTransform IKFootRootTransform(Output.Pose.GetComponentSpaceTransform(IKFootRootBoneIndex));
				IKFootRootTransform.SetRotation(BoneRotation * IKFootRootTransform.GetRotation());
				IKFootRootTransform.NormalizeRotation();
				Output.Pose.SetComponentSpaceTransform(IKFootRootBoneIndex, IKFootRootTransform);

				// IK Feet 
				// These match the root orientation, so don't rotate them. Just preserve root rotation. 
				// We need to update their translation though, since we rotated their parent (the IK Foot Root bone).
				if (bUpdateIKFootBones)
				{
					const FQuat IKFootRotation = FQuat(RotationAxis, -YawAngleRadians * IKFootRootOrientationAlpha);

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

			// Clear time accumulator, to be filled during next update.
			CachedDeltaTime = 0.f;
		}
	}
}

bool FAnimNode_OrientationWarping::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	bool bIKFootRootIsValid = IKFootRootBoneIndex != INDEX_NONE;
	bool bIKFeetAreValid = IKFootBoneIndexArray.Num() > 0;
	for (auto& IKFootBoneIndex : IKFootBoneIndexArray)
	{
		bIKFeetAreValid = bIKFeetAreValid && IKFootBoneIndex != INDEX_NONE;
	}

	bool bSpineIsValid = SpineBoneDataArray.Num() > 0;
	for (auto& Spine : SpineBoneDataArray)
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
