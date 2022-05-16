// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Units/RigUnit.h"
#include "RigUnit_AnimAttribute.generated.h"

/**
 * Sets the value of an animation attribute with the matching names.
 * If the attribute was not found, a new attribute is created.
 *
 * Animation Attributes allow dynamically added data to flow from
 * one Anim Node to other Anim Nodes downstream in the Anim Graph.
 */
USTRUCT(meta=(Abstract, Category="Animation Attribute", TemplateName="Set Attribute", NodeColor = "0.0 0.36470600962638855 1.0", Varying))
struct FRigUnit_SetAnimAttributeBase : public FRigUnitMutable
{
	GENERATED_BODY()
};


USTRUCT(meta=(DisplayName="Set Attribute - Integer"))
struct CONTROLRIG_API FRigUnit_SetAnimAttribute_Integer: public FRigUnit_SetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_SetAnimAttribute_Integer()
	{
		Name = TEXT("NewAttributeName");
		BoneName = NAME_None;
		Value = 0;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	int32 Value;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Set Attribute - Float"))
struct CONTROLRIG_API FRigUnit_SetAnimAttribute_Float: public FRigUnit_SetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_SetAnimAttribute_Float()
	{
		Name = TEXT("NewAttributeName");
		BoneName = NAME_None;
		Value = 0.f;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Set Attribute - Transform"))
struct CONTROLRIG_API FRigUnit_SetAnimAttribute_Transform: public FRigUnit_SetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_SetAnimAttribute_Transform()
	{
		Name = TEXT("NewAttributeName");
		BoneName = NAME_None;
		Value = FTransform::Identity;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FTransform Value;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};

USTRUCT(meta=(DisplayName="Set Attribute - Vector"))
struct CONTROLRIG_API FRigUnit_SetAnimAttribute_Vector: public FRigUnit_SetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_SetAnimAttribute_Vector()
	{
		Name = TEXT("NewAttributeName");
		BoneName = NAME_None;
		Value = FVector::ZeroVector;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FVector Value;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};

USTRUCT(meta=(DisplayName="Set Attribute - Quaternion"))
struct CONTROLRIG_API FRigUnit_SetAnimAttribute_Quaternion: public FRigUnit_SetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_SetAnimAttribute_Quaternion()
	{
		Name = TEXT("NewAttributeName");
		BoneName = NAME_None;
		Value = FQuat::Identity;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FQuat Value;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};

/**
 * Gets the value of an animation attribute with the matching names.
 * If the attribute was not found, the fallback value is outputted.
 *
 * Animation Attributes allow dynamically added data to flow from
 * one Anim Node to other Anim Nodes downstream in the Anim Graph.
 */
USTRUCT(meta=(Abstract, Category="Animation Attribute", TemplateName="Get Attribute", NodeColor="0.462745, 1,0, 0.329412", Varying))
struct FRigUnit_GetAnimAttributeBase : public FRigUnit
{
	GENERATED_BODY()
};


USTRUCT(meta=(DisplayName="Get Attribute - Integer"))
struct CONTROLRIG_API FRigUnit_GetAnimAttribute_Integer: public FRigUnit_GetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_GetAnimAttribute_Integer()
	{
		Name = TEXT("AttributeName");
		BoneName = NAME_None;
		FallbackValue = 0;
		Value = 0;
		bWasFound = false;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	int32 FallbackValue;
	
	UPROPERTY(meta=(Output))
	int32 Value;

	UPROPERTY(meta=(Output))
	bool bWasFound;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Get Attribute - Float"))
struct CONTROLRIG_API FRigUnit_GetAnimAttribute_Float: public FRigUnit_GetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_GetAnimAttribute_Float()
	{
		Name = TEXT("AttributeName");
		BoneName = NAME_None;
		FallbackValue = 0.f;
		Value = 0.f;
		bWasFound = false;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	float FallbackValue;
	
	UPROPERTY(meta=(Output))
	float Value;

	UPROPERTY(meta=(Output))
	bool bWasFound;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Get Attribute - Transform"))
struct CONTROLRIG_API FRigUnit_GetAnimAttribute_Transform: public FRigUnit_GetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_GetAnimAttribute_Transform()
	{
		Name = TEXT("AttributeName");
		BoneName = NAME_None;
		FallbackValue = FTransform::Identity;
		Value = FTransform::Identity;
		bWasFound = false;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FTransform FallbackValue;
	
	UPROPERTY(meta=(Output))
	FTransform Value;

	UPROPERTY(meta=(Output))
	bool bWasFound;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Get Attribute - Vector"))
struct CONTROLRIG_API FRigUnit_GetAnimAttribute_Vector: public FRigUnit_GetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_GetAnimAttribute_Vector()
	{
		Name = TEXT("AttributeName");
		BoneName = NAME_None;
		FallbackValue = FVector::ZeroVector;
		Value = FVector::ZeroVector;
		bWasFound = false;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FVector FallbackValue;
	
	UPROPERTY(meta=(Output))
	FVector Value;

	UPROPERTY(meta=(Output))
	bool bWasFound;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};


USTRUCT(meta=(DisplayName="Get Attribute - Quaternion"))
struct CONTROLRIG_API FRigUnit_GetAnimAttribute_Quaternion: public FRigUnit_GetAnimAttributeBase
{
	GENERATED_BODY()
	
	FRigUnit_GetAnimAttribute_Quaternion()
	{
		Name = TEXT("AttributeName");
		BoneName = NAME_None;
		FallbackValue = FQuat::Identity;
		Value = FQuat::Identity;
		bWasFound = false;
		CachedBoneName = NAME_None;
		CachedBoneIndex = 0;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;
	
	UPROPERTY(meta=(Input, CustomWidget="BoneName"))
	FName BoneName;

	UPROPERTY(meta=(Input))
	FQuat FallbackValue;
	
	UPROPERTY(meta=(Output))
	FQuat Value;

	UPROPERTY(meta=(Output))
	bool bWasFound;

	UPROPERTY()
	FName CachedBoneName;
	
	UPROPERTY()
	int32 CachedBoneIndex;
};