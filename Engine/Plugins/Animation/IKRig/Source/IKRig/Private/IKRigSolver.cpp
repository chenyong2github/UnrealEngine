// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IKRigSolver.cpp: Solver definition class
=============================================================================*/

#include "IKRigSolver.h"
#include "IKRigSolverDefinition.h"
#include "IKRigDataTypes.h"

// input hierarchy and ref pose? 
void UIKRigSolver::Init(UIKRigSolverDefinition* InSolverDefinition, FIKRigTransformGetter InRefPoseGetter, FIKRigGoalGetter InGoalGetter)
{
	SolverDefinition = InSolverDefinition;

	RefPoseGetter = InRefPoseGetter;
	ensure(RefPoseGetter.IsBound());

	GoalGetter = InGoalGetter;
	ensure(GoalGetter.IsBound());

	InitInternal();
}

bool UIKRigSolver::IsSolverActive() const 
{
	return (SolverDefinition && bEnabled);
}

// input : goal getter or goals
// output : modified pose - GlobalTransforms
void UIKRigSolver::Solve(FIKRigTransformModifier& InOutGlobalTransform)
{
	if (IsSolverActive())
	{
		SolveInternal(InOutGlobalTransform);
	}
}

bool UIKRigSolver::GetEffectorTarget(const FIKRigEffector& InEffector, FIKRigTarget& OutTarget) const
{
	if (SolverDefinition && GoalGetter.IsBound())
	{	
		const FName* GoalName = SolverDefinition->GetEffectorToGoal().Find(InEffector);
		if (GoalName)
		{
			return GoalGetter.Execute(*GoalName, OutTarget);
		}
	}

	return false;
}

const FIKRigTransform& UIKRigSolver::GetReferencePose() const
{
	if (RefPoseGetter.IsBound())
	{
		return RefPoseGetter.Execute();
	}

	static FIKRigTransform Dummy;
	return Dummy;
}