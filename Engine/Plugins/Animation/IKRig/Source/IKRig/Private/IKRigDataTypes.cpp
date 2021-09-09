// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDataTypes.h"

void FIKRigGoalContainer::InitializeFromGoals(const TArray<UIKRigEffectorGoal*>& InGoals)
{
	Goals.Empty(InGoals.Num());
	for (const UIKRigEffectorGoal* Goal : InGoals)
	{
		Goals.Emplace(Goal->GoalName, Goal);
	}
}

void FIKRigGoalContainer::SetIKGoal(const FIKRigGoal& InGoal)
{
	Goals.Add(InGoal.Name, InGoal);
}

void FIKRigGoalContainer::SetIKGoal(const UIKRigEffectorGoal* InEffectorGoal)
{
	if (FIKRigGoal* Goal = Goals.Find(InEffectorGoal->GoalName))
	{
		if (InEffectorGoal->PreviewMode == EIKRigGoalPreviewMode::Absolute)
		{
			Goal->Position = InEffectorGoal->CurrentTransform.GetTranslation();
			Goal->Rotation = InEffectorGoal->CurrentTransform.Rotator();
			Goal->PositionSpace = EIKRigGoalSpace::Component;
			Goal->RotationSpace = EIKRigGoalSpace::Component;
		}
		else
		{
			Goal->Position = InEffectorGoal->CurrentTransform.GetTranslation() - InEffectorGoal->InitialTransform.GetTranslation();
			const FQuat RelativeRotation = InEffectorGoal->CurrentTransform.GetRotation() * InEffectorGoal->InitialTransform.GetRotation().Inverse();
			Goal->Rotation = RelativeRotation.Rotator();
			Goal->PositionSpace = EIKRigGoalSpace::Additive;
			Goal->RotationSpace = EIKRigGoalSpace::Additive;
		}
		
        Goal->PositionAlpha = InEffectorGoal->PositionAlpha;
        Goal->RotationAlpha = InEffectorGoal->RotationAlpha;
	}
	else
	{
		Goals.Emplace(InEffectorGoal->GoalName, InEffectorGoal);
	}
}

bool FIKRigGoalContainer::GetGoalByName(const FName& InGoalName, FIKRigGoal& OutGoal) const
{
	if (const FIKRigGoal* Goal = Goals.Find(InGoalName))
	{
		OutGoal = *Goal;
		return true;
	}

	return false;
}