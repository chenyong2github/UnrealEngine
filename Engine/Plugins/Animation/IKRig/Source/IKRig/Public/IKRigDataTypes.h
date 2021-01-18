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
	FRotator Rotation;
};

USTRUCT()
struct IKRIG_API FIKRigEffector
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Guid;

	UPROPERTY(EditAnywhere, Category = FIKRigEffector)
	FName Bone;

	FIKRigEffector()
		: Guid(FGuid::NewGuid())
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FIKRigEffector& Effector)
	{
		return Ar << Effector.Guid;
	}

	FORCEINLINE bool operator==(const FIKRigEffector& Rhs) const
	{
		return Guid == Rhs.Guid;
	}
};

USTRUCT()
struct IKRIG_API FIKRigGoalContainer
{
	GENERATED_BODY()

private:

	TMap<FName, FIKRigGoal> Goals;

public:

	void SetAllGoals(const TMap<FName, FIKRigGoal> &InGoals)
	{
		Goals = InGoals;
	}

	void SetGoalTransform(
		const FName& GoalName,
		const FVector& InPosition,
		const FRotator& InRotation)
	{
		if (FIKRigGoal* Goal = Goals.Find(GoalName))
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
};

template <typename ValueType>
struct TIKRigEffectorMapKeyFuncs : public TDefaultMapKeyFuncs<const FIKRigEffector, ValueType, false>
{
	static FORCEINLINE FIKRigEffector	GetSetKey(TPair<FIKRigEffector, ValueType> const& Element) { return Element.Key; }
	static FORCEINLINE uint32			GetKeyHash(FIKRigEffector const& Key) { return GetTypeHash(Key.Guid); }
	static FORCEINLINE bool				Matches(FIKRigEffector const& A, FIKRigEffector const& B) { return (A.Guid == B.Guid); }
};

template <typename ValueType>
using TIKRigEffectorMap = TMap<FIKRigEffector, ValueType, FDefaultSetAllocator, TIKRigEffectorMapKeyFuncs<ValueType>>;


struct IKRIG_API FIKRigTransforms
{
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

