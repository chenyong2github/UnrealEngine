// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDefinition.h"
#include "IKRigController.generated.h"

class UIKRigSolverDefinition;
class UIKRigBoneSetting;
struct FIKRigHierarchy;
struct FReferenceSkeleton;
struct FIKRigConstraintProfile;


UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIGEDITOR_API UIKRigController : public UObject
{
	GENERATED_BODY()

public:

	//** use this to get a handle to a controller for the given IKRig */
	static UIKRigController* GetControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition);

	//** skeleton */
	void SetSkeleton(const FReferenceSkeleton& InSkeleton) const;
	FIKRigSkeleton& GetSkeleton() const;

	//** solvers */
	UIKRigSolver* AddSolver(TSubclassOf<UIKRigSolver> InSolverClass);
	void RemoveSolver(UIKRigSolver* SolverToDelete);
	UIKRigSolver* GetSolver(int32 Index) const;
	int32 GetNumSolvers() const;

	//** per-bone settings and constraints */
	UIKRigBoneSetting* AddBoneSetting(TSubclassOf<UIKRigBoneSetting> NewBoneSettingType) const;

	//** goals */
	void RenameGoal(const FName& OldName, const FName& NewName) const;
	void GetGoalNames(TArray<FName>& OutGoals) const;
	DECLARE_MULTICAST_DELEGATE(FGoalModified);
	FGoalModified OnGoalModified;

	// BEGIN UObject
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// END UObject

private:

	UPROPERTY(transient)
	UIKRigDefinition* IKRigDefinition = nullptr;

	static TMap<UIKRigDefinition*, UIKRigController*> DefinitionToControllerMap;
	void SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition);

	//** called by IKRigDefinition when it's begin destroy */
	static void RemoveControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition);

	friend class UIKRigDefinition;
};

   