// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/ConstraintSolver.h"
#include "IKRigBoneSetting.h"

void UIKRigConstraintSolver::Initialize(const FIKRigSkeleton& InSkeleton)
{
	// TODO get constraints that this solver can deal with
}

void UIKRigConstraintSolver::Solve(
	FIKRigSkeleton& IKRigSkeleton,
	const FIKRigGoalContainer& Goals,
	FControlRigDrawInterface* InOutDrawInterface)
{
	/*
	FIKRigConstraintProfile* Current = ConstraintProfiles.Find(ActiveProfile);

	if (Current)
	{
		for (auto Iter = Current->Constraints.CreateIterator(); Iter; ++Iter)
		{
			UIKRigConstraint* Constraint = Iter.Value();
			if (Constraint)
			{
				// @todo: later add inoutdrawinterface
				Constraint->Apply(InOutGlobalTransform, InOutDrawInterface);
			}
		}
	}*/
}

void UIKRigConstraintSolver::CollectGoalNames(TSet<FIKRigEffectorGoal>& OutGoals) const
{

}