// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Definition 
 *
 */

#pragma once

#include "IKRigSolverDefinition.h"
#include "IKRigConstraintDefinition.generated.h"

class UIKRigConstraint;

USTRUCT()
struct IKRIG_API FIKRigConstraintProfile 
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, instanced, Category=FIKRigConstraintProfile)
	TMap<FName, UIKRigConstraint*> Constraints;
};

UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIG_API UIKRigConstraintDefinition : public UIKRigSolverDefinition
{
	GENERATED_BODY()

public: 
	UIKRigConstraintDefinition();

private:
	// at least one profile always exists - Default
	UPROPERTY(EditAnywhere, Category = "Profile")
	TMap<FName, FIKRigConstraintProfile> ConstraintProfiles;
	friend class UIKRigConstraintSolver;
	friend class UIKRigController;
public:
	static const FName DefaultProfileName;
};