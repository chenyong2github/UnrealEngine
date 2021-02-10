// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDataTypes.h"
#include "IKRigSolver.generated.h"

struct FIKRigTarget;
struct FIKRigTransform;
struct FIKRigTransforms;
struct FControlRigDrawInterface;

USTRUCT()
struct IKRIG_API FIKRigEffectorGoal
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = FIKRigEffector)
	FName Goal;

	UPROPERTY(EditAnywhere, Category = FIKRigEffector)
	FName Bone;
};

// run time processor 
UCLASS(abstract, hidecategories = UObject)
class IKRIG_API UIKRigSolver : public UObject
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bEnabled = true;

	/** override Init() to setup internal data based on ref pose */
	virtual void Init(const FIKRigTransforms& InGlobalTransform) PURE_VIRTUAL("Init");

	/** override Solve() to evaluate new output pose (InOutGlobalTransform) */
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransform,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) PURE_VIRTUAL("Solve");

	/** override CollectGoalNames() to tell the processor which goals to collect for this solver
	 * NOTE: only ADD to OutGoals, do not reset or remove */
	virtual void CollectGoalNames(TSet<FName>& OutGoals) const PURE_VIRTUAL("CollectGoalNames");

	#if WITH_EDITOR
	/** override RenameGoal() when UI renames a goal, you can auto-update any effectors
	 * that were previously mapped to the old goal name. */
	virtual void RenameGoal(const FName& OldName, const FName& NewName) {};
	#endif // WITH_EDITOR
	
protected:

	static bool GetGoalForEffector(
		const FIKRigEffectorGoal& InEffector,
		const FIKRigGoalContainer &Goals,
		FIKRigGoal& OutGoal)
	{
		return Goals.GetGoalByName(InEffector.Goal, OutGoal);
	}
};

