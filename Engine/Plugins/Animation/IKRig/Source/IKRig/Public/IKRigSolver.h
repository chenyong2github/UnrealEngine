// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDataTypes.h"
#include "IKRigSolver.generated.h"

struct FIKRigEffector;
struct FIKRigTarget;
struct FIKRigTransform;
struct FIKRigTransformModifier;
struct FControlRigDrawInterface;

// run time processor 
UCLASS(Abstract, hidecategories = UObject)
class IKRIG_API UIKRigSolver : public UObject
{
	GENERATED_BODY()

public: 
	/** Required delegates to run this solver */
	DECLARE_DELEGATE_RetVal(const FIKRigTransform&, FIKRigTransformGetter);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FIKRigGoalGetter, const FName& /*InGoalName*/, FIKRigTarget& /*OutTarget*/);

	// input hierarchy and ref pose? 
	void Init(const FIKRigTransformModifier& TransformModifier, FIKRigTransformGetter InRefPoseGetter, FIKRigGoalGetter InGoalGetter/*, FSolveConstraint& InConstraintHandler*/);

	// input : goal getter or goals
	// output : modified pose - GlobalTransforms
	// or use SolverInternal function
	void Solve(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface);

	const TIKRigEffectorMap<FName>& GetEffectorToGoal() const
	{
		return EffectorToGoal;
	}

	void CollectGoals(TArray<FName>& OutGoals);

protected:

	virtual void InitInternal(const FIKRigTransformModifier& InGlobalTransform) {};
	virtual void SolveInternal(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface) {};
	virtual bool IsSolverActive() const;

	bool GetEffectorTarget(const FIKRigEffector& InEffector, FIKRigTarget& OutTarget) const;
	const FIKRigTransform& GetReferencePose() const;

	UPROPERTY(EditAnywhere, Category = "Definition")
	bool bEnabled = true;

	// effector name to goals name map
	TIKRigEffectorMap<FName> EffectorToGoal;

private:

	// delegate
	FIKRigTransformGetter RefPoseGetter;
	FIKRigGoalGetter GoalGetter;

	/// BEGIN UObject
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	/// END UObject

	friend class UIKRigController;

#if WITH_EDITOR
private:
	void RenameGoal(const FName& OldName, const FName& NewName);
	// get unique name delegate by IKRigDefinition
	void EnsureUniqueGoalName(FName& InOutGoalName) const;

	DECLARE_MULTICAST_DELEGATE(FGoalNeedsUpdate);
	FGoalNeedsUpdate GoalNeedsUpdateDelegate;

protected:
	FName CreateUniqueGoalName(const TCHAR* Suffix) const;
	void OnGoalHasBeenUpdated();
	void EnsureToAddEffector(const FIKRigEffector& InEffector, const FString& InPrefix);
	void EnsureToRemoveEffector(const FIKRigEffector& InEffector);

	virtual void UpdateEffectors() {};
#endif // WITH_EDITOR
};

