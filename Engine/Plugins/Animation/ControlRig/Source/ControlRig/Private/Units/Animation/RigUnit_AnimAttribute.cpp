// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_AnimAttribute.h"

#include "Animation/BuiltInAttributeTypes.h"
#include "Units/RigUnitContext.h"
#include "Components/SkeletalMeshComponent.h"

template<typename T>
class TAnimAttributeType;

template<>
class TAnimAttributeType<int>
{
public:
	using Type = FIntegerAnimationAttribute;
};

template<>
class TAnimAttributeType<float>
{
public:
	using Type = FFloatAnimationAttribute;
};

template<>
class TAnimAttributeType<FTransform>
{
public:
	using Type = FTransformAnimationAttribute;
};

template<>
class TAnimAttributeType<FVector>
{
public:
	using Type = FVectorAnimationAttribute;
};

template<>
class TAnimAttributeType<FQuat>
{
public:
	using Type = FQuaternionAnimationAttribute;
};


template<typename T>
FORCEINLINE_DEBUGGABLE static T* GetAnimAttributeValue(
	bool bAddIfNotFound,
	const FRigUnitContext& Context,
	const FName& Name,
	const FName& BoneName,
	FName& CachedBoneName,
	int32& CachedBoneIndex)
{
	if (Name.IsNone())
	{
		return nullptr;
	}

	if (!Context.AnimAttributeContainer)
	{
		return nullptr;
	}
	
	const USkeletalMeshComponent* OwningComponent = Cast<USkeletalMeshComponent>(Context.OwningComponent);

	if (!OwningComponent ||
		!OwningComponent->GetSkeletalMeshAsset())
	{
		return nullptr;
	}

	if (BoneName == NAME_None)
	{
		// default to use root bone
		CachedBoneIndex = 0;
	}
	else
	{
		// Invalidate cache if input changed
		if (CachedBoneName != BoneName)
		{
			CachedBoneIndex = OwningComponent->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(BoneName);
		}
	}
	
	CachedBoneName = BoneName;

	if (CachedBoneIndex != INDEX_NONE)
	{
		const UE::Anim::FAttributeId Id = {Name, FCompactPoseBoneIndex(CachedBoneIndex)} ;
		typename TAnimAttributeType<T>::Type* Attribute = bAddIfNotFound ?
			Context.AnimAttributeContainer->FindOrAdd<typename TAnimAttributeType<T>::Type>(Id) :
			Context.AnimAttributeContainer->Find<typename TAnimAttributeType<T>::Type>(Id);
		if (Attribute)
		{
			return &Attribute->Value;
		}
	}

	return nullptr;
}


FRigUnit_SetAnimAttribute_Integer_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	
	int* ValuePtr = GetAnimAttributeValue<int>(true ,Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	if (ValuePtr)
	{
		*ValuePtr = Value;
	}
}

FRigUnit_SetAnimAttribute_Float_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	float* ValuePtr = GetAnimAttributeValue<float>(true, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	if (ValuePtr)
	{
		*ValuePtr = Value;
	}
}

FRigUnit_SetAnimAttribute_Transform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FTransform* ValuePtr = GetAnimAttributeValue<FTransform>(true, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	if (ValuePtr)
	{
		*ValuePtr = Value;
	}
}

FRigUnit_SetAnimAttribute_Vector_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FVector* ValuePtr = GetAnimAttributeValue<FVector>(true, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	if (ValuePtr)
	{
		*ValuePtr = Value;
	}
}

FRigUnit_SetAnimAttribute_Quaternion_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	FQuat* ValuePtr = GetAnimAttributeValue<FQuat>(true, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	if (ValuePtr)
	{
		*ValuePtr = Value;
	}
}

FRigUnit_GetAnimAttribute_Integer_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	
	const int* ValuePtr = GetAnimAttributeValue<int>(false ,Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	bWasFound = ValuePtr ? true : false;
	Value = ValuePtr ? *ValuePtr : FallbackValue;
}

FRigUnit_GetAnimAttribute_Float_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	const float* ValuePtr = GetAnimAttributeValue<float>(false ,Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	bWasFound = ValuePtr ? true : false;
	Value = ValuePtr ? *ValuePtr : FallbackValue;
}

FRigUnit_GetAnimAttribute_Transform_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	const FTransform* ValuePtr = GetAnimAttributeValue<FTransform>(false, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	bWasFound = ValuePtr ? true : false;
	Value = ValuePtr ? *ValuePtr : FallbackValue;
}

FRigUnit_GetAnimAttribute_Vector_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	const FVector* ValuePtr = GetAnimAttributeValue<FVector>(false, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	bWasFound = ValuePtr ? true : false;
	Value = ValuePtr ? *ValuePtr : FallbackValue;	
}

FRigUnit_GetAnimAttribute_Quaternion_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	const FQuat* ValuePtr = GetAnimAttributeValue<FQuat>(false, Context, Name, BoneName, CachedBoneName, CachedBoneIndex);
	bWasFound = ValuePtr ? true : false;
	Value = ValuePtr ? *ValuePtr : FallbackValue;	
}

