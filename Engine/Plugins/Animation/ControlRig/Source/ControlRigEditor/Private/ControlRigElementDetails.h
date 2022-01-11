// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchy.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "DetailsViewWrapperObject.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "IPropertyUtilities.h"
#include "SSearchableComboBox.h"
#include "DetailsViewWrapperObject.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "SAdvancedTransformInputBox.h"

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

	/** Helper buttons. */
	TSharedPtr<SButton> UseSelectedButton;
	TSharedPtr<SButton> SelectElementButton;
	FSlateColor OnGetWidgetForeground(const TSharedPtr<SButton> Button) const;
	FSlateColor OnGetWidgetBackground(const TSharedPtr<SButton> Button) const;
	FReply OnGetSelectedClicked();
	FReply OnSelectInHierarchyClicked();
	
	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> NameHandle;
	TArray<TSharedPtr<FString>> ElementNameList;
	UControlRigBlueprint* BlueprintBeingCustomized;
	TSharedPtr<SSearchableComboBox> SearchableComboBox;
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
	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);

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

class FRigComputedTransformDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FRigComputedTransformDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:

	TSharedPtr<IPropertyHandle> TransformHandle;
	FEditPropertyChain PropertyChain;
	UControlRigBlueprint* BlueprintBeingCustomized;

	void OnTransformChanged(FEditPropertyChain* InPropertyChain);
};

class FRigBaseElementDetails : public IDetailCustomization
{
public:

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	URigHierarchy* GetHierarchy() const { return HierarchyBeingCustomized; }
	FRigElementKey GetElementKey() const;
	FText GetName() const;
	void SetName(const FText& InNewText, ETextCommit::Type InCommitType);
	bool OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage);

	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	bool IsSetupModeEnabled() const;

	TArray<FRigElementKey> GetElementKeys() const;

	template<typename T>
	TArray<T> GetElementsInDetailsView() const
	{
		TArray<T> Elements;
		for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
		{
			if(ObjectBeingCustomized.IsValid())
			{
				Elements.Add(ObjectBeingCustomized->GetContent<T>());
			}
		}
		return Elements;
	}

	template<typename T>
	TArray<T*> GetElementsInHierarchy() const
	{
		TArray<T*> Elements;
		if(HierarchyBeingCustomized)
		{
			for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
			{
				if(ObjectBeingCustomized.IsValid())
				{
					Elements.Add(HierarchyBeingCustomized->Find<T>(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetKey()));
				}
			}
		}
		return Elements;
	}

	bool IsAnyElementOfType(ERigElementType InType) const;
	bool IsAnyElementNotOfType(ERigElementType InType) const;
	bool IsAnyControlOfType(ERigControlType InType) const;
	bool IsAnyControlNotOfType(ERigControlType InType) const;

	static void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule);
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass);

protected:

	UControlRigBlueprint* BlueprintBeingCustomized;
	URigHierarchy* HierarchyBeingCustomized;
	TArray<TWeakObjectPtr<UDetailsViewWrapperObject>> ObjectsBeingCustomized;
};

class FRigTransformElementDetails : public FRigBaseElementDetails
{
public:

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** FRigBaseElementDetails interface */
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass) override;

	virtual void CustomizeTransform(IDetailLayoutBuilder& DetailBuilder);

protected:

	bool IsCurrentLocalEnabled() const;

	void AddChoiceWidgetRow(IDetailCategoryBuilder& InCategory, const FText& InSearchText, TSharedRef<SWidget> InWidget);
};

class FRigBoneElementDetails : public FRigTransformElementDetails
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigBoneElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

class FRigControlElementDetails : public FRigTransformElementDetails
{
public:

	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigControlElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** FRigBaseElementDetails interface */
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass) override;

	bool IsShapeEnabled() const;
	bool IsEnabled(ERigControlValueType InValueType) const;

	const TArray<TSharedPtr<FString>>& GetShapeNameList() const;
	const TArray<TSharedPtr<FString>>& GetControlTypeList() const;

	FText GetDisplayName() const;
	void SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType);

	void OnCopyShapeProperties();
	void OnPasteShapeProperties();

private:
	TArray<TSharedPtr<FString>> ShapeNameList;
	static TArray<TSharedPtr<FString>> ControlTypeList;
	TSharedPtr<FRigInfluenceEntryModifier> InfluenceModifier;
	TSharedPtr<FStructOnScope> InfluenceModifierStruct;

	TSharedPtr<IPropertyHandle> ShapeNameHandle;
	TSharedPtr<IPropertyHandle> ShapeColorHandle;
	TSharedPtr<IPropertyHandle> ShapeTransformHandle;
};

class FRigNullElementDetails : public FRigTransformElementDetails
{
public:

	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigNullElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
