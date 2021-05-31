// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "IKRigController.generated.h"

struct FReferenceSkeleton;
struct FIKRigSkeleton;
class UIKRigSolver;
class UIKRigDefinition;
class UIKRigEffectorGoal;
class USkeletalMesh;

UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIGEDITOR_API UIKRigController : public UObject
{
	GENERATED_BODY()

public:

	//** use this to get a handle to a controller for the given IKRig */
	static UIKRigController* GetIKRigController(UIKRigDefinition* InIKRigDefinition);
	//** get the asset this controller controls */
	UIKRigDefinition* GetAsset() const;

	//** skeleton */
	void SetSourceSkeletalMesh(USkeletalMesh* SkeletalMesh, bool bReImportBones) const;
	USkeletalMesh* GetSourceSkeletalMesh() const;
	void SetSkeleton(const FReferenceSkeleton& InSkeleton) const;
	FIKRigSkeleton& GetSkeleton() const;
	//** END skeleton */

	//** solvers */
	int32 AddSolver(TSubclassOf<UIKRigSolver> InSolverClass) const;
	void RemoveSolver(UIKRigSolver* SolverToDelete) const;
	bool MoveSolverInStack(int32 SolverToMoveIndex, int32 TargetSolverIndex) const;
	UIKRigSolver* GetSolver(int32 Index) const;
	int32 GetNumSolvers() const;
	void SetRootBone(const FName& RootBoneName, int32 SolverIndex) const;
	const TArray<UIKRigSolver*>& GetSolverArray() const;
	//** END solvers */

	//** goals */
	UIKRigEffectorGoal* AddNewGoal(const FName& GoalName, const FName& BoneName) const;
	bool RemoveGoal(const FName& GoalName) const;
	FName RenameGoal(const FName& OldName, const FName& PotentialNewName) const;
	bool SetGoalBone(const FName& GoalName, const FName& NewBoneName) const;
	FName GetBoneForGoal(const FName& GoalName) const;
	bool ConnectGoalToSolver(const UIKRigEffectorGoal& Goal, int32 SolverIndex) const;
	bool DisconnectGoalFromSolver(const FName& GoalToRemove, int32 SolverIndex) const;
	bool IsGoalConnectedToSolver(const FName& Goal, int32 SolverIndex) const;
	int32 GetGoalIndex(const FName& GoalName) const;
	FName GetGoalName(const int32& GoalIndex) const;
	const TArray<UIKRigEffectorGoal*>& GetAllGoals() const;
	UIKRigEffectorGoal* GetGoal(int32 GoalIndex) const;
	UIKRigEffectorGoal* GetGoal(const FName& GoalName) const;
	UObject* GetEffectorForGoal(const FName& GoalName, int32 SolverIndex) const;
	FTransform GetGoalInitialTransform(const FName& GoalName) const;
	FTransform GetGoalCurrentTransform(const FName& GoalName) const;
	void SetGoalInitialTransform(const FName& GoalName, const FTransform& Transform) const;
	void SetGoalCurrentTransform(const FName& GoalName, const FTransform& Transform) const;
	static void SanitizeGoalName(FString& InOutName);
	DECLARE_MULTICAST_DELEGATE(FGoalModified);
	FGoalModified OnGoalModified;
	//** END goals */

	//** bone settings */
	void AddBoneSetting(const FName& BoneName, int32 SolverIndex) const;
	void RemoveBoneSetting(const FName& BoneName, int32 SolverIndex) const;
	bool CanAddBoneSetting(const FName& BoneName, int32 SolverIndex) const;
	bool CanRemoveBoneSetting(const FName& BoneName, int32 SolverIndex) const;
	UObject* GetSettingsForBone(const FName& BoneName, int32 SolverIndex) const;
	bool DoesBoneHaveSettings(const FName& BoneName) const;
	//** END bone settings */

	// BEGIN UObject
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// END UObject

private:

	UPROPERTY(transient)
	UIKRigDefinition* IKRigAsset = nullptr;

	// todo, I'm not convinced Controller functions shouldn't just live on the asset itself...
	static TMap<UIKRigDefinition*, UIKRigController*> AssetToControllerMap;

	friend class UIKRigDefinition;
};
