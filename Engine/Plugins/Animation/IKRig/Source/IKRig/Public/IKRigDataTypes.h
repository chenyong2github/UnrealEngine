// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "IKRigDataTypes.generated.h"

struct FIKRigHierarchy;

USTRUCT()
struct IKRIG_API FIKRigGoal
{
	GENERATED_BODY()

		FIKRigGoal()
		: Position(ForceInitToZero),
		Rotation(ForceInitToZero)
	{
		Name = NAME_None;
	}
	FIKRigGoal(const FName& GoalName)
		: Position(ForceInitToZero),
		Rotation(ForceInitToZero)
	{
		Name = GoalName;
	}

	UPROPERTY(VisibleAnywhere, Category = FIKRigGoal)
		FName Name;

	UPROPERTY(EditAnywhere, Category = FIKRigGoal)
		FVector Position;

	UPROPERTY(EditAnywhere, Category = FIKRigGoal)
		FQuat Rotation;
};

inline uint32 GetTypeHash(FIKRigGoal ObjectRef) { return GetTypeHash(ObjectRef.Name); }

USTRUCT()
struct IKRIG_API FIKRigGoalContainer
{
	GENERATED_BODY()

		void InitializeGoalsFromNames(const TArray<FName>& InGoalNames)
	{
		Goals.Reserve(InGoalNames.Num());
		for (const FName& Name : InGoalNames)
		{
			Goals.Emplace(Name, Name);
		}
	}

	void SetGoalTransform(
		const FName& GoalName,
		const FVector& InPosition,
		const FQuat& InRotation)
	{
		FIKRigGoal* Goal = Goals.Find(GoalName);
		if (Goal)
		{
			Goal->Position = InPosition;
			Goal->Rotation = InRotation;
		}
	}

	bool GetGoalByName(const FName& InGoalName, FIKRigGoal& OutGoal) const
	{
		const FIKRigGoal* Goal = Goals.Find(InGoalName);
		if (Goal)
		{
			OutGoal = *Goal;
			return true;
		}

		return false;
	}

	void GetNames(TArray<FName>& OutNames) const
	{
		Goals.GenerateKeyArray(OutNames);
	}

	FORCEINLINE int GetNumGoals() const
	{
		return Goals.Num();
	}

private:

	TMap<FName, FIKRigGoal> Goals;
};

USTRUCT()
struct IKRIG_API FIKRigTransforms
{
	GENERATED_BODY()
	
	FIKRigTransforms()
		: Hierarchy(nullptr)
	{
	}

	FIKRigTransforms(const FIKRigHierarchy* InHierarchy);

	void SetGlobalTransform(int32 Index, const FTransform& InTransform, bool bPropagate);
	void SetLocalTransform(int32 Index, const FTransform& InTransform, bool bUpdate);
	
	const FTransform& GetLocalTransform(int32 Index) const; // this is mutating, but const_cast 
	const FTransform& GetGlobalTransform(int32 Index) const;

	void SetAllGlobalTransforms(const TArray<FTransform>& InTransforms);

	const FIKRigHierarchy* Hierarchy;

private:

	TArray<FTransform> GlobalTransforms;
	TArray<FTransform> LocalTransforms;
	TBitArray<>	LocalTransformDirtyFlags;

	void EnsureLocalTransformsExist();
	void RecalculateLocalTransform();
	void UpdateLocalTransform(int32 Index);

	/** this function propagates to children and updates LocalTransforms */
	void SetGlobalTransform_Internal(int32 Index, const FTransform& InTransform);
	void SetGlobalTransform_Recursive(int32 Index, const FTransform& InTransform);

	FTransform GetRelativeTransform(int32 Index, int32 BaseIndex) const;
};

