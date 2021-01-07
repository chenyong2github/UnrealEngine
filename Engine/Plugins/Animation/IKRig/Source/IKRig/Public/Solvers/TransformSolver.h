// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains Transform Solver Execution
 *
 */

#pragma once

#include "IKRigSolver.h"
#include "TransformSolver.generated.h"


// run time for UTransformSolverDefinition
UCLASS()
class IKRIG_API UTransformSolver : public UIKRigSolver
{
	GENERATED_BODY()

protected:
	virtual void InitInternal(const FIKRigTransformModifier& InGlobalTransform) override;
	virtual void SolveInternal(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface) override;
	virtual bool IsSolverActive() const override;
};

