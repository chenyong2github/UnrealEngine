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
