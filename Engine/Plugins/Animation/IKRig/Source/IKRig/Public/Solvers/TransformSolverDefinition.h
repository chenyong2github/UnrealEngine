// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Solver Definition 
 *
 */

#pragma once

#include "IKRigSolverDefinition.h"
#include "TransformSolverDefinition.generated.h"

UCLASS(BlueprintType)
class IKRIG_API UTransformSolverDefinition : public UIKRigSolverDefinition
{
	GENERATED_BODY()

public:
	UTransformSolverDefinition();

	// align is terrible on this
	UPROPERTY(EditAnywhere, Category = "Solver")
	bool	bEnablePosition = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool	bEnableRotation = true;

private:
#if WITH_EDITOR
	virtual void UpdateTaskList() override;

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// UObject interface
#endif // WITH_EDITOR

public:
	static const FName TransformTarget;
};