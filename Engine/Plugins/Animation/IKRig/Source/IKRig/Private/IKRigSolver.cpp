// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSolver.h"
#include "IKRigDataTypes.h"


/* TODO KIARAN: do we even want to auto rename goals?
 * I think the risk is high of being frustrating for user
#if WITH_EDITOR

void UIKRigSolver::RenameGoal(const FName& OldName, const FName& NewName)
{
	for (auto Iter = EffectorToGoalName.CreateIterator(); Iter; ++Iter)
	{
		if (Iter.Value() == OldName)
		{
			Iter.Value() = NewName;
		}
	}
}

#endif // WITH_EDITOR
*/