// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDataTypes.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "IKRigProcessor.generated.h"

class UIKRigConstraintSolver;
class UIKRigDefinition;
class UIKRigSolver;
struct FControlRigDrawInterface;


UCLASS(BlueprintType)
class IKRIG_API UIKRigProcessor : public UObject
{
	GENERATED_BODY()

private:

	UPROPERTY(EditAnywhere, Category = "RigDefinition")
	UIKRigDefinition* RigDefinition = nullptr;

	UPROPERTY(transient)
	TArray<FTransform> RefPoseTransforms;
	FIKRigTransforms Transforms;

	UPROPERTY(transient)
	TArray<UIKRigSolver*> Solvers;

	FControlRigDrawInterface DrawInterface;
	bool bInitialized = false;

public: 

	UPROPERTY(transient)
	FIKRigGoalContainer Goals;

	/** setup and run */
	void Initialize(UIKRigDefinition* InRigDefinition);
	void Solve();

	FIKRigTransforms& GetTransforms();
	const FIKRigHierarchy* GetHierarchy() const;
	void ResetToRefPose();
	const FControlRigDrawInterface& GetDrawInterface() { return DrawInterface; }
};

