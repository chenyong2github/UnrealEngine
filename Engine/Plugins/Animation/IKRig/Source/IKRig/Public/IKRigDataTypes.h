// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Definition 
 *
 *  https://docs.google.com/document/d/1yd8GCfT2aufxSdb5jAzlNTr1SptxEFpS9pWdQY-8LIk/edit#
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "IKRigDataTypes.generated.h"

struct FIKRigHierarchy;

USTRUCT()
struct IKRIG_API FIKRigPosition
{
	GENERATED_BODY()

	FIKRigPosition()
		: Position(ForceInitToZero)
	{

	}
	UPROPERTY(EditAnywhere, Category = FIKRigPosition)
	FVector Position;
};

USTRUCT()
struct IKRIG_API FIKRigRotation
{
	GENERATED_BODY()

	FIKRigRotation()
		: Rotation(ForceInitToZero)
	{

	}

	UPROPERTY(EditAnywhere, Category = FIKRigRotation)
	FRotator Rotation;
};

// list of targets for goal
// this contains default value for the target
USTRUCT()
struct IKRIG_API FIKRigTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = FIKRigTarget)
	FName Bone;

	UPROPERTY(EditAnywhere, Category = FIKRigTarget)
	FIKRigPosition PositionTarget;

	UPROPERTY(EditAnywhere, Category = FIKRigTarget)
	FIKRigRotation RotationTarget;
};

USTRUCT()
struct IKRIG_API FIKRigGoal
{
	GENERATED_BODY()

	FIKRigGoal()
	{
		Name = NAME_None;
	}
	FIKRigGoal(const FName& GoalName)
	{
		Name = GoalName;
	}

	UPROPERTY(VisibleAnywhere, Category=FIKRigGoal)
	FName Name;

	UPROPERTY(EditAnywhere, Category = FIKRigGoal)
	FIKRigTarget Target;

	void SetPositionTarget(const FVector& InPosition)
	{
		Target.PositionTarget.Position = InPosition;
	}

	void SetRotationTarget(const FRotator& InRotation)
	{
		Target.RotationTarget.Rotation = InRotation;
	}
};

USTRUCT()
struct IKRIG_API FIKRigEffector
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Guid;

	FIKRigEffector()
	{
		Guid = FGuid::NewGuid();
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

template <typename ValueType>
struct TIKRigEffectorMapKeyFuncs : public TDefaultMapKeyFuncs<const FIKRigEffector, ValueType, false>
{
	static FORCEINLINE FIKRigEffector					GetSetKey(TPair<FIKRigEffector, ValueType> const& Element) { return Element.Key; }
	static FORCEINLINE uint32							GetKeyHash(FIKRigEffector const& Key) { return GetTypeHash(Key.Guid); }
	static FORCEINLINE bool								Matches(FIKRigEffector const& A, FIKRigEffector const& B) { return (A.Guid == B.Guid); }
};

template <typename ValueType>
using TIKRigEffectorMap = TMap<FIKRigEffector, ValueType, FDefaultSetAllocator, TIKRigEffectorMapKeyFuncs<ValueType>>;

// If you want to modify this, use FIKRigTransformModifier
USTRUCT()
struct IKRIG_API FIKRigTransform
{
	GENERATED_BODY()

private:
	UPROPERTY(VisibleAnywhere, Category = FIKRigTransform)
	TArray<FTransform> GlobalTransforms;

public:
	int32 GetNum() const 
	{
		return GlobalTransforms.Num();
	}

	bool IsValidIndex(int32 Index) const
	{
		return GlobalTransforms.IsValidIndex(Index);
	}

	const TArray<FTransform>& GetGlobalTransforms() const
	{
		return GlobalTransforms;
	}

	const FTransform& GetGlobalTransform(int32 Index) const
	{
		if (GlobalTransforms.IsValidIndex(Index))
		{
			return GlobalTransforms[Index];
		}

		return FTransform::Identity;
	}

	FTransform GetRelativeTransform(int32 Index, int32 BaseIndex) const
	{
		if (GlobalTransforms.IsValidIndex(Index))
		{
			if (GlobalTransforms.IsValidIndex(BaseIndex))
			{
				return (GlobalTransforms[Index].GetRelativeTransform(GlobalTransforms[BaseIndex]));
			}
			else
			{
				return GlobalTransforms[Index];
			}
		}

		return FTransform::Identity;
	}
	friend struct FIKRigTransformModifier;
	friend class UIKRigDefinition;
	friend class UIKRigController;
};

// allows transform to be modified 
// use this class to modify transform
struct IKRIG_API FIKRigTransformModifier
{
	FIKRigTransformModifier()
		: Hierarchy(nullptr)
	{
	}

	FIKRigTransformModifier(const FIKRigHierarchy* InHierarchy);

	void SetGlobalTransform(int32 Index, const FTransform& InTransform, bool bPropagate);
	void SetLocalTransform(int32 Index, const FTransform& InTransform, bool bUpdate);
	// this is mutating, but const_cast 
	const FTransform& GetLocalTransform(int32 Index) const;
	const FTransform& GetGlobalTransform(int32 Index) const;

	void ResetGlobalTransform(const FIKRigTransform& InTransform);

	const FIKRigHierarchy* Hierarchy;

private:

	// we use this info to modify 
	// ensure memory cycle for these are valid through
	FIKRigTransform GlobalTransforms;
	TArray<FTransform> LocalTransforms;
	TBitArray<>	LocalTransformDirtyFlags;

	void EnsureLocalTransformsExist();
	void RecalculateLocalTransform();
	void UpdateLocalTransform(int32 Index);
	/* This function does propagate through children
	 * of the current index and keeps last LocalTransform to up to date
	 */
	void SetGlobalTransform_Internal(int32 Index, const FTransform& InTransform);

	/** Set Global Transform Recursive 
	 *	
	 */
	void SetGlobalTransform_Recursive(int32 Index, const FTransform& InTransform);
};

