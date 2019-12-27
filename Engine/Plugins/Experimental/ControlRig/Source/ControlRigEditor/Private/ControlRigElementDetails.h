// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"

class IPropertyHandle;

UENUM()
enum class ERigElementDetailsTransformComponent : uint8
{
	TranslationX,
	TranslationY,
	TranslationZ,
	RotationRoll,
	RotationPitch,
	RotationYaw,
	ScaleX,
	ScaleY,
	ScaleZ
};

class FRigElementDetails : public IDetailCustomization
{
public:

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	FRigElementKey GetElementKey() const { return ElementKeyBeingCustomized; }
	FRigHierarchyContainer* GetHierarchy() const { return ContainerBeingCustomized; }
	FText GetName() const { return FText::FromName(GetElementKey().Name); }
	void SetName(const FText& InNewText, ETextCommit::Type InCommitType);

	static float GetTransformComponent(const FTransform& InTransform, ERigElementDetailsTransformComponent InComponent);
	static void SetTransformComponent(FTransform& OutTransform, ERigElementDetailsTransformComponent InComponent, float InNewValue);

protected:

	FRigElementKey ElementKeyBeingCustomized;
	UControlRigBlueprint* BlueprintBeingCustomized;
	FRigHierarchyContainer* ContainerBeingCustomized;
};

class FRigBoneDetails : public FRigElementDetails
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigBoneDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
};

class FRigControlDetails : public FRigElementDetails
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigControlDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	TOptional<float> GetComponentValue(bool bInitial, ERigElementDetailsTransformComponent Component) const;
	void SetComponentValue(float InNewValue, ETextCommit::Type InCommitType, bool bInitial, ERigElementDetailsTransformComponent Component);

	const TArray<TSharedPtr<FString>>& GetGizmoNameList() const;

private:
	TArray<TSharedPtr<FString>> GizmoNameList;
};

class FRigSpaceDetails : public FRigElementDetails
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigSpaceDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
