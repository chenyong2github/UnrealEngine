// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigConstraint Definition Implementation
 *
 */

#include "IKRigConstraintDefinition.h"

const FName UIKRigConstraintDefinition::DefaultProfileName = FName(TEXT("Default"));

UIKRigConstraintDefinition::UIKRigConstraintDefinition()
{
	FIKRigConstraintProfile& DefaultProfile = ConstraintProfiles.Add(DefaultProfileName);
	DefaultProfile.Name = DefaultProfileName;
}
