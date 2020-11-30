// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Solver Definition 
 *
 */

#pragma once

#include "CoreMinimal.h"

#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "IKRigSolverDefinition.generated.h"

class UIKRigSolver;

// do we save data between Goal it cares?
// data type
UCLASS(Blueprintable, BlueprintType)
class IKRIG_API UIKRigSolverDefinition : public UObject
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, Category = "Definition")
	FString DisplayName;

	// effector name to goals name map
	UPROPERTY(VisibleAnywhere, Category = "Definition")
	TMap<FName, FName> TaskToGoal;

	// this is name of constraints it's used
	// use Delegate from IKRigDefinition to query and solve
	UPROPERTY(EditAnywhere, Category = "Definition")
	TArray<FName> Constraints;

	UPROPERTY(VisibleAnywhere, Category = "Definition")
	TSubclassOf<UIKRigSolver> ExecutionClass;

public:
	const TMap<FName, FName>& GetTaskToGoal() const
	{
		return TaskToGoal;
	}

	TSubclassOf<UIKRigSolver> GetExecutionClass() 
	{		
		return ExecutionClass;		
	}

	// if you have auto configuration option, return true;
	virtual bool CanAutoConfigure() const
	{
		return false;
	}

	virtual void AutoConfigure()
	{
		ensureMsgf(false, TEXT("(%s)Auto Configuration needs to be impelmeneted."), *GetName());
	}

	void CollectGoals(TArray<FName>& OutGoals);

private:
#if WITH_EDITOR
	void RenameGoal(const FName& OldName, const FName& NewName);
	// get unique name delegate by IKRigDefinition
	void EnsureUniqueGoalName(FName& InOutGoalName) const;

	DECLARE_MULTICAST_DELEGATE(FGoalNeedsUpdate);
	FGoalNeedsUpdate GoalNeedsUpdateDelegate;

protected:
	FName CreateUniqueGoalName(const TCHAR* Suffix) const;
	void OnGoalHasBeenUpdated();

	virtual void UpdateTaskList() {};
#endif // WITH_EDITOR

	/// BEGIN UObject
	virtual void PostLoad() override;
	/// END UObject
	friend class UIKRigController;
};

