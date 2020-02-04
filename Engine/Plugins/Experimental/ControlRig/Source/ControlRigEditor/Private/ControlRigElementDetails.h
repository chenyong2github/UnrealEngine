// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "IPropertyUtilities.h"

class IPropertyHandle;

class FRigUnitDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigUnitDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:

	TSharedRef<SWidget> MakeNameListItemWidget(TSharedPtr<FString> InItem);
	FText GetNameListText(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty) const;
	TSharedPtr<FString> GetCurrentlySelectedItem(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList) const;
	void SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnNameListComboBox(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList);

	UControlRigBlueprint* BlueprintBeingCustomized;
	UControlRigGraph* GraphBeingCustomized;
	TMap<FName, TSharedPtr<SControlRigGraphPinNameListValueWidget>> NameListWidgets;
};

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

	ECheckBoxState GetComponentValueBool(bool bInitial) const;
	void SetComponentValueBool(ECheckBoxState InNewValue, bool bInitial);
	TOptional<float> GetComponentValueFloat(ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component) const;
	void SetComponentValueFloat(float InNewValue, ETextCommit::Type InCommitType, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component);
	void SetComponentValueFloat(float InNewValue, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component);
	bool IsGizmoEnabled() const;
	bool IsEnabled(ERigControlValueType InValueType) const;
	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);

	const TArray<TSharedPtr<FString>>& GetGizmoNameList() const;
	const TArray<TSharedPtr<FString>>& GetControlTypeList() const;

private:
	TArray<TSharedPtr<FString>> GizmoNameList;
	static TArray<TSharedPtr<FString>> ControlTypeList;
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
