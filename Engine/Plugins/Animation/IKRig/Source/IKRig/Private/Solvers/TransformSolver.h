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
	virtual void InitInternal() override;
	virtual void SolveInternal(FIKRigTransformModifier& InOutGlobalTransform) override;
	virtual bool IsSolverActive() const override;
};

