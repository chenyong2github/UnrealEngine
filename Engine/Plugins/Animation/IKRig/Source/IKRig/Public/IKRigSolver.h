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
struct FIKRigTransforms;
struct FControlRigDrawInterface;

// run time processor 
UCLASS(Abstract, hidecategories = UObject)
class IKRIG_API UIKRigSolver : public UObject
{
	GENERATED_BODY()

public:

	/** wraps Solver::Solve() */
	void SolveInternal(
		FIKRigTransforms& InOutGlobalTransform, 
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface);
	void AppendGoalNamesToArray(TArray<FName>& OutGoals);

protected:

	/** override Init(), Solve() and IsSolverActive() in subclasses */
	virtual void Init(const FIKRigTransforms& InGlobalTransform) {};
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransform,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) {};
	virtual bool IsSolverActive() const;

	bool GetGoalForEffector(
		const FIKRigEffector& InEffector, 
		const FIKRigGoalContainer &Goals, 
		FIKRigGoal& OutGoal) const;

	UPROPERTY(EditAnywhere, Category = "Definition")
	bool bEnabled = true;

	// effector name to goals name map
	TIKRigEffectorMap<FName> EffectorToGoalName;

private:

	// BEGIN UObject
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	// END UObject

	friend class UIKRigController;
	friend class UIKRigProcessor;

#if WITH_EDITOR

private:

	void RenameGoal(const FName& OldName, const FName& NewName);
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

