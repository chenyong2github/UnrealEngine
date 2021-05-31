// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/IKRig_SetTransform.h"
#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"


UIKRig_SetTransform::UIKRig_SetTransform()
{
	Effector = CreateDefaultSubobject<UIKRig_SetTransformEffector>(TEXT("Effector"));
}

void UIKRig_SetTransform::Initialize(const FIKRigSkeleton& IKRigSkeleton)
{
	BoneIndex = IKRigSkeleton.GetBoneIndexFromName(Bone);
}

void UIKRig_SetTransform::Solve(FIKRigSkeleton& IKRigSkeleton, const FIKRigGoalContainer& Goals)
{
	FIKRigGoal OutGoal;
	if (!Goals.GetGoalByName(Goal, OutGoal))
	{
		return;
	}

	// check that settings are such that there is anything to do at all
	const bool bAnythingEnabled = Effector->bEnablePosition || Effector->bEnableRotation;
	const bool bHasAlpha = Effector->Alpha > KINDA_SMALL_NUMBER;
	if (!(bAnythingEnabled && bHasAlpha))
	{
		return;
	}

	FTransform& CurrentTransform = IKRigSkeleton.CurrentPoseGlobal[BoneIndex];	

	if (Effector->bEnablePosition)
	{
		const FVector TargetPosition = FMath::Lerp(CurrentTransform.GetTranslation(), OutGoal.FinalBlendedPosition, Effector->Alpha);
		CurrentTransform.SetLocation(TargetPosition);
	}
	
	if (Effector->bEnableRotation)
	{
		const FQuat TargetRotation = FMath::Lerp(CurrentTransform.GetRotation(), OutGoal.FinalBlendedRotation, Effector->Alpha);
		CurrentTransform.SetRotation(TargetRotation);
	}
	
	IKRigSkeleton.PropagateGlobalPoseBelowBone(BoneIndex);
}

void UIKRig_SetTransform::UpdateSolverSettings(UIKRigSolver* InSettings)
{
	if (UIKRig_SetTransform* Settings = Cast<UIKRig_SetTransform>(InSettings))
	{
		Effector->bEnablePosition = Settings->Effector->bEnablePosition;
		Effector->bEnableRotation = Settings->Effector->bEnableRotation;
		Effector->Alpha = Settings->Effector->Alpha;
	}
}

void UIKRig_SetTransform::AddGoal(const UIKRigEffectorGoal& NewGoal)
{
	Goal = NewGoal.GoalName;
	Bone = NewGoal.BoneName;
}

void UIKRig_SetTransform::RemoveGoal(const FName& GoalName)
{
	if (Goal == GoalName)
	{
		Goal = NAME_None;
		Bone = NAME_None;
	}	
}

void UIKRig_SetTransform::RenameGoal(const FName& OldName, const FName& NewName)
{
	if (Goal == OldName)
	{
		Goal = NewName;
	}
}

void UIKRig_SetTransform::SetGoalBone(const FName& GoalName, const FName& NewBoneName)
{
	if (Goal == GoalName)
	{
		Bone = NewBoneName;
	}
}

bool UIKRig_SetTransform::IsGoalConnected(const FName& GoalName) const
{
	return Goal == GoalName;
}

UObject* UIKRig_SetTransform::GetEffectorWithGoal(const FName& GoalName)
{
	return Goal == GoalName ? Effector : nullptr;
}

bool UIKRig_SetTransform::IsBoneAffectedBySolver(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton) const
{
	return IKRigSkeleton.IsBoneInDirectLineage(BoneName, Bone);
}
