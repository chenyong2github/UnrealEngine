// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "IPropertyUtilities.h"

class IPropertyHandle;

class FRigElementKeyDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FRigElementKeyDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:

	ERigElementType GetElementType() const;
	FString GetElementName() const;
	void SetElementName(FString InName);
	void UpdateElementNameList();
	void OnElementNameChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectionInfo);
	TSharedRef<SWidget> OnGetElementNameWidget(TSharedPtr<FString> InItem);
	FText GetElementNameAsText() const;

	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> NameHandle;
	TArray<TSharedPtr<FString>> ElementNameList;
	UControlRigBlueprint* BlueprintBeingCustomized;
};

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
	static float GetEulerTransformComponent(const FEulerTransform& InTransform, ERigElementDetailsTransformComponent InComponent);
	static void SetEulerTransformComponent(FEulerTransform& OutTransform, ERigElementDetailsTransformComponent InComponent, float InNewValue);
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

	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnAffectedListChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);

private:

	TSharedPtr<FRigInfluenceEntryModifier> InfluenceModifier;
	TSharedPtr<FStructOnScope> InfluenceModifierStruct;
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

	FText GetDisplayName() const;
	void SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	ECheckBoxState GetComponentValueBool(bool bInitial) const;
	void SetComponentValueBool(ECheckBoxState InNewValue, bool bInitial, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	TOptional<float> GetComponentValueFloat(ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component) const;
	void SetComponentValueFloat(float InNewValue, ETextCommit::Type InCommitType, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	void SetComponentValueFloat(float InNewValue, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	TOptional<int32> GetComponentValueInteger(ERigControlValueType InValueType) const;
	void SetComponentValueInteger(int32 InNewValue, ERigControlValueType InValueType, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	bool IsGizmoEnabled() const;
	bool IsEnabled(ERigControlValueType InValueType) const;
	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnAffectedListChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	int32 GetControlEnumValue(ERigControlValueType InValueType) const;
	void OnControlEnumChanged(int32 InValue, ESelectInfo::Type InSelectInfo, ERigControlValueType InValueType, const TSharedRef<IPropertyUtilities> PropertyUtilities);

	const TArray<TSharedPtr<FString>>& GetGizmoNameList() const;
	const TArray<TSharedPtr<FString>>& GetControlTypeList() const;

private:
	TArray<TSharedPtr<FString>> GizmoNameList;
	static TArray<TSharedPtr<FString>> ControlTypeList;
	TSharedPtr<FRigInfluenceEntryModifier> InfluenceModifier;
	TSharedPtr<FStructOnScope> InfluenceModifierStruct;
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
