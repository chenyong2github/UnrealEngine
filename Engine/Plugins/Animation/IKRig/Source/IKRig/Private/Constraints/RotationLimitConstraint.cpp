// Copyright Epic Games, Inc. All Rights Reserved.

#include "RotationLimitConstraint.h"
#include "IKRigHierarchy.h"
#include "IKRigDataTypes.h"
#include "IKRig.h"

void URotationLimitConstraint::SetupInternal(const FIKRigTransformModifier& InOutTransformModifier)
{
	if (TargetBone != NAME_None)
	{
		const int32 TargetBoneIndex = InOutTransformModifier.Hierarchy->GetIndex(TargetBone);
		if (TargetBoneIndex != INDEX_NONE)
		{
			// looking for local transform
			const int32 TargetParentBoneIndex = InOutTransformModifier.Hierarchy->GetParentIndex(TargetBone);
			// for now we only constraint to parent
			BaseIndex = TargetParentBoneIndex;
			ConstrainedIndex = TargetBoneIndex;

			// if we support constraint to different joint, this function has to change to get relative transform
			FTransform LocalTransform = InOutTransformModifier.GetLocalTransform(TargetBoneIndex);

			// set rotation frame
			BaseFrameOffset = FQuat(Offset);
			RelativelRefPose = LocalTransform;
		}
	}
}

void URotationLimitConstraint::Apply(FIKRigTransformModifier& InOutTransformModifier, FControlRigDrawInterface* InOutDrawInterface)
{
	if (ConstrainedIndex != INDEX_NONE)
	{
		FTransform LocalTransform = InOutTransformModifier.GetLocalTransform(ConstrainedIndex);
		// ref pose?
		// just do rotation for now
		FQuat LocalRotation = BaseFrameOffset.Inverse() * LocalTransform.GetRotation(); // child rotation
		LocalRotation.Normalize();
		FQuat LocalRefRotation = BaseFrameOffset.Inverse() * RelativelRefPose.GetRotation(); // later, maybe we'll make it more generic, so it doesn't always have to be just local transform
		LocalRefRotation.Normalize();
		bool bRotationChanged = false;

		auto SetLimitedQuat = [&](EAxis::Type InAxis, float InLimtAngle)
		{
			FQuat DeltaQuat = LocalRefRotation.Inverse() * LocalRotation;

			FVector RefTwistAxis = FMatrix::Identity.GetUnitAxis(InAxis);
			FQuat Twist, Swing;
			DeltaQuat.ToSwingTwist(RefTwistAxis, Swing, Twist);
			float SwingAngle = Swing.GetAngle();
			float TwistAngle = Twist.GetAngle();

			UE_LOG(LogIKRig, Log, TEXT("Delta Decomposition : Swing %s (%f), Twist %s (%f)"),
				*Swing.GetRotationAxis().ToString(), FMath::RadiansToDegrees(SwingAngle),
				*Twist.GetRotationAxis().ToString(), FMath::RadiansToDegrees(TwistAngle));

			// we deal with the extra ones left
			LocalRotation = LocalRefRotation * Swing * FQuat(Twist.GetRotationAxis(), FMath::Clamp(TwistAngle, -InLimtAngle, InLimtAngle));
			LocalRotation.Normalize();
			bRotationChanged = true;
		};

		if (bXLimitSet)
		{
			SetLimitedQuat(EAxis::X, Limit.X);
		}

		if (bYLimitSet)
		{
			SetLimitedQuat(EAxis::Y, Limit.Y);
		}

		if (bZLimitSet)
		{
			SetLimitedQuat(EAxis::Z, Limit.Z);
		}


		if (bRotationChanged)
		{
			LocalRotation = BaseFrameOffset * LocalRotation;

			if ((LocalRotation | LocalTransform.GetRotation()) < 0.f)
			{
				LocalRotation *= -1.f;
			}

			LocalTransform.SetRotation(LocalRotation);
			LocalRotation.Normalize();

			InOutTransformModifier.SetLocalTransform(ConstrainedIndex, LocalTransform, true);
		}
	}
}