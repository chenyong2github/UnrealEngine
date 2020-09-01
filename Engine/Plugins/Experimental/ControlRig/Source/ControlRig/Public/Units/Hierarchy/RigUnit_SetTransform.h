// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetTransform.generated.h"

/**
 * SetTransform is used to set a single transform on hierarchy.
 */
USTRUCT(meta=(DisplayName="Set Transform", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="SetBoneTransform,SetControlTransform,SetInitialTransform,SetSpaceTransform", Varying))
struct FRigUnit_SetTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetTransform()
		: Item(NAME_None, ERigElementType::Bone)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, bInitial(false)
		, Transform(FTransform::Identity)
		, Weight(1.f)
		, bPropagateToChildren(false)
		, CachedIndex()
	{}

	virtual FString GetUnitLabel() const override;

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if(Space == EBoneGetterSetterMode::LocalSpace)
		{
			if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
			{
				return Container->GetParentKey(Item);
			}
		}
		return FRigElementKey();
	}


	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to set the transform for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * Defines if the transform should be set in local or global space
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	/**
	 * Defines if the transform should be set as current (false) or initial (true).
	 * Initial transforms for bones and other elements in the hierarchy represent the reference pose's value.
	 */ 
	UPROPERTY(meta = (Input))
	bool bInitial;

	// The new transform of the given item
	UPROPERTY(meta=(Input))
	FTransform Transform;

	// Defines how much the change will be applied
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// If set to true children of affected items in the hierarchy
	// will follow the transform change - otherwise only the parent will move.
	UPROPERTY(meta=(Input))
	bool bPropagateToChildren;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};

/**
 * SetTranslation is used to set a single translation on hierarchy.
 */
USTRUCT(meta=(DisplayName="Set Translation", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="SetBoneTranslation,SetControlTranslation,SetInitialTranslation,SetSpaceTranslation,SetBoneLocation,SetControlLocation,SetInitialLocation,SetSpaceLocation,SetBonePosition,SetControlPosition,SetInitialPosition,SetSpacePosition,SetTranslation,SetLocation,SetPosition", Varying))
struct FRigUnit_SetTranslation : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetTranslation()
		: Item(NAME_None, ERigElementType::Bone)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, Translation(FVector::ZeroVector)
		, Weight(1.f)
		, bPropagateToChildren(false)
		, CachedIndex()
	{}

	virtual FString GetUnitLabel() const override;

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if(Space == EBoneGetterSetterMode::LocalSpace)
		{
			if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
			{
				return Container->GetParentKey(Item);
			}
		}
		return FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to set the translation for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * Defines if the translation should be set in local or global space
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// The new translation of the given item
	UPROPERTY(meta=(Input))
	FVector Translation;

	// Defines how much the change will be applied
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// If set to true children of affected items in the hierarchy
	// will follow the transform change - otherwise only the parent will move.
	UPROPERTY(meta=(Input))
	bool bPropagateToChildren;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};

/**
 * SetRotation is used to set a single rotation on hierarchy.
 */
USTRUCT(meta=(DisplayName="Set Rotation", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="SetBoneRotation,SetControlRotation,SetInitialRotation,SetSpaceRotation,SetBoneOrientation,SetControlOrientation,SetInitialOrientation,SetSpaceOrientation,SetRotation,SetOrientation", Varying))
struct FRigUnit_SetRotation : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetRotation()
		: Item(NAME_None, ERigElementType::Bone)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, Rotation(FQuat::Identity)
		, Weight(1.f)
		, bPropagateToChildren(false)
		, CachedIndex()
	{}

	virtual FString GetUnitLabel() const override;

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if(Space == EBoneGetterSetterMode::LocalSpace)
		{
			if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
			{
				return Container->GetParentKey(Item);
			}
		}
		return FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to set the rotation for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * Defines if the rotation should be set in local or global space
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// The new rotation of the given item
	UPROPERTY(meta=(Input))
	FQuat Rotation;

	// Defines how much the change will be applied
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// If set to true children of affected items in the hierarchy
	// will follow the transform change - otherwise only the parent will move.
	UPROPERTY(meta=(Input))
	bool bPropagateToChildren;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};

/**
 * SetScale is used to set a single scale on hierarchy.
 */
USTRUCT(meta=(DisplayName="Set Scale", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="SetBoneScale,SetControlScale,SetInitialScale,SetSpaceScale,SetScale", Varying))
struct FRigUnit_SetScale : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetScale()
		: Item(NAME_None, ERigElementType::Bone)
		, Space(EBoneGetterSetterMode::GlobalSpace)
		, Scale(FVector::OneVector)
		, Weight(1.f)
		, bPropagateToChildren(false)
		, CachedIndex()
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to set the scale for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * Defines if the scale should be set in local or global space
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// The new scale of the given item
	UPROPERTY(meta=(Input))
	FVector Scale;

	// Defines how much the change will be applied
	UPROPERTY(meta = (Input, UIMin = "0.0", UIMax = "1.0"))
	float Weight;

	// If set to true children of affected items in the hierarchy
	// will follow the transform change - otherwise only the parent will move.
	UPROPERTY(meta=(Input))
	bool bPropagateToChildren;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};
