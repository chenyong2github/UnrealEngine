// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigConstraint Definition Implementation
 *
 */

#include "IKRigConstraintDefinition.h"
#include "IKRigConstraintSolver.h"

const FName UIKRigConstraintDefinition::DefaultProfileName = FName(TEXT("Default"));

UIKRigConstraintDefinition::UIKRigConstraintDefinition()
{
	FIKRigConstraintProfile& DefaultProfile = ConstraintProfiles.Add(DefaultProfileName);

	DisplayName = TEXT("Constraint Solver");
	ExecutionClass = UIKRigConstraintSolver::StaticClass();
}
