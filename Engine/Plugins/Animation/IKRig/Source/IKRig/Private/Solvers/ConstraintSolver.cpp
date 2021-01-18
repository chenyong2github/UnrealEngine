// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/ConstraintSolver.h"
#include "IKRigBoneSetting.h"

/*
// register/unregister query function
void UIKRigConstraintSolver::RegisterQueryConstraintHandler(const FIKRigQueryConstraint& InQueryConstraintHandler)
{

}

void UIKRigConstraintSolver::UnregisterQueryConstraintHandler()
{

}
*/

void UIKRigConstraintSolver::Init(const FIKRigTransforms& InGlobalTransform)
{
	/*
	if (ConstraintDefinition)
	{
		// we duplicate and create a copy of the objects, so that we can mutate
		ConstraintProfiles = ConstraintDefinition->ConstraintProfiles;
		for (auto Iter = ConstraintProfiles.CreateIterator(); Iter; ++Iter)
		{
			FIKRigConstraintProfile& CurrentProfile = Iter.Value();
			for (auto InnerIter = CurrentProfile.Constraints.CreateIterator(); InnerIter; ++InnerIter)
			{
				UIKRigConstraint*& Template = InnerIter.Value();
				UIKRigConstraint* CopyConstraint = NewObject<UIKRigConstraint>(this, Template->GetClass(), TEXT("IKRigConstraint"), RF_NoFlags, Template);
				CopyConstraint->Setup(InGlobalTransform);
				// replace value
				Template = CopyConstraint;
			}
		}

		// go through all UIKRigConstraint and create copy
		ActiveProfile = UIKRigConstraintDefinition::DefaultProfileName;
	}*/
}

void UIKRigConstraintSolver::Solve(
	FIKRigTransforms& InOutGlobalTransform,
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

/*
void UIKRigConstraintSolver::SolverConstraints(const TArray<FName>& ConstraintsList, FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface)
{
	FIKRigConstraintProfile* Current = ConstraintProfiles.Find(ActiveProfile);

	if (Current)
	{
		for (const FName& ConstraintName : ConstraintsList)
		{
			UIKRigConstraint** Constraint = Current->Constraints.Find(ConstraintName);
			if (Constraint)
			{
				// @todo: later add inoutdrawinterface
				(*Constraint)->Apply(InOutGlobalTransform, InOutDrawInterface);
			}
		}
	}
}
*/