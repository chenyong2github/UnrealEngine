// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetConnectionConfigTypeCustomization.h"

#include "BaseWidgetBlueprint.h"
#include "ConnectionTargetNodeBuilder.h"
#include "Customizations/StateSwitcher/SStringSelectionComboBox.h"
#include "LogVCamEditor.h"
#include "UI/Switcher/VCamStateSwitcherWidget.h"
#include "UI/Switcher/WidgetConnectionConfig.h"
#include "UI/VCamWidget.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/PropertyIterator.h"

#define LOCTEXT_NAMESPACE "FWidgetConnectionConfigCustomization"

namespace UE::VCamCoreEditor::Private
{
	TSharedRef<IPropertyTypeCustomization> FWidgetConnectionConfigTypeCustomization::MakeInstance()
	{
		return MakeShared<FWidgetConnectionConfigTypeCustomization>();
	}

	void FWidgetConnectionConfigTypeCustomization::CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		HeaderRow.
			NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				PropertyHandle->CreatePropertyValueWidget()
			];
	}

	void FWidgetConnectionConfigTypeCustomization::CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		// Retrieve structure's child properties
		uint32 NumChildren;
		StructPropertyHandle->GetNumChildren( NumChildren );	
		const FName WidgetProperty = GET_MEMBER_NAME_CHECKED(FWidgetConnectionConfig, Widget);
		const FName ConnectionTargetsProperty = GET_MEMBER_NAME_CHECKED(FWidgetConnectionConfig, ConnectionTargets);

		TSharedPtr<IPropertyHandle> WidgetReferencePropertyHandle;
		TSharedPtr<IPropertyHandle> ConnectionTargetsPropertyHandle;
		
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedPtr<IPropertyHandle> ChildProperty = StructPropertyHandle->GetChildHandle(ChildIndex);
			if (ChildProperty->GetProperty() && ChildProperty->GetProperty()->GetFName() == WidgetProperty)
			{
				WidgetReferencePropertyHandle = ChildProperty;
			}
			else if (ChildProperty->GetProperty() && ChildProperty->GetProperty()->GetFName() == ConnectionTargetsProperty)
			{
				ConnectionTargetsPropertyHandle = ChildProperty;
			}
		}
		
		IDetailPropertyRow& ReferenceRow = ChildBuilder.AddProperty(WidgetReferencePropertyHandle.ToSharedRef());
		CustomizeWidgetReferenceProperty(WidgetReferencePropertyHandle.ToSharedRef(), ReferenceRow, CustomizationUtils);
		
		CustomizeConnectionTargetsReferenceProperty(StructPropertyHandle, ConnectionTargetsPropertyHandle.ToSharedRef(), ChildBuilder, CustomizationUtils);
	}

	void FWidgetConnectionConfigTypeCustomization::CustomizeWidgetReferenceProperty(
		TSharedRef<IPropertyHandle> WidgetReferencePropertyHandle,
		IDetailPropertyRow& Row,
		IPropertyTypeCustomizationUtils& CustomizationUtils) const
	{
		WidgetReferencePropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([PropertyUtils = CustomizationUtils.GetPropertyUtilities()]()
		{
			PropertyUtils->ForceRefresh();
		}));

		const TWeakPtr<IPropertyUtilities> WeakPropertyUtils = CustomizationUtils.GetPropertyUtilities();
		Row.CustomWidget()
			.NameContent()
			[
				WidgetReferencePropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SStringSelectionComboBox)
					.ToolTipText(LOCTEXT("PropertySelection.Tooltip", "Select a child widget from this Blueprint.\n\nThis can either be:\n\t- an auto-generated variable (see \"Is Variable\" check box in Designer)\n\t- a regular object property of type VCamWidget (you must make sure the property points to something valid before you change states)"))
					.SelectedItem_Lambda([WidgetReferencePropertyHandle]()
					{
						FName Name;
						WidgetReferencePropertyHandle->GetValue(Name);
						return Name.ToString();
					})
					// Property list is only build once: when the Blueprint is recompiled,
					// the details view SHOULD refresh automatically and thus all of this is reconstructed. 
					.ItemList(this, &FWidgetConnectionConfigTypeCustomization::GetPropertyItemList, WeakPropertyUtils)
					.OnItemSelected_Lambda([WidgetReferencePropertyHandle](const FString& SelectedItem)
					{
						WidgetReferencePropertyHandle->SetValue(SelectedItem);
					})
					.Font(CustomizationUtils.GetRegularFont())
			];
	}
	
	TArray<FString> FWidgetConnectionConfigTypeCustomization::GetPropertyItemList(TWeakPtr<IPropertyUtilities> WeakPropertyUtils) const
	{
		TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtils.Pin();
		if (!PropertyUtilities)
		{
			return {};
		}
		
		TArray<FString> PropertyItemList;
		PropertyItemList.Add(FName(NAME_None).ToString());
		
		const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyUtilities->GetSelectedObjects();
		if (!ensure(SelectedObjects.Num() == 1) || !SelectedObjects[0].IsValid())
		{
			return PropertyItemList;
		}

		UObject* Object = SelectedObjects[0].Get();
		UUserWidget* Widget = Cast<UUserWidget>(Object);
		if (!Widget)
		{
			return {};
		}

		// Two use cases are expected: Editing an instance from 1. UMG Designer tab 2. Class Defaults in Graph tab
		// When editing Class Defaults, the WidgetTree is expected to be nullptr when editing class defaults so let's grab it from the asset.
		const TObjectPtr<UWidgetTree> WidgetTree = Widget->WidgetTree
			? Widget->WidgetTree
			: GetWidgetTreeThroughBlueprintAsset(Widget);
		if (!WidgetTree)
		{
			return {};
		}
		
		WidgetTree->ForEachWidget([&PropertyItemList](UWidget* Widget)
		{
			const bool bIsVCamWidgetProperty = Widget && Widget->GetClass()->IsChildOf(UVCamWidget::StaticClass());
			if (bIsVCamWidgetProperty)
			{
				PropertyItemList.Add(Widget->GetName());
			}
		});
		return PropertyItemList;
	}

	TObjectPtr<UWidgetTree> FWidgetConnectionConfigTypeCustomization::GetWidgetTreeThroughBlueprintAsset(UUserWidget* ClassDefaultWidget)
	{
		if (!ensure(ClassDefaultWidget->HasAnyFlags(RF_ClassDefaultObject)))
		{
			return nullptr;
		}
		
		UObject* Blueprint = ClassDefaultWidget->GetClass()->ClassGeneratedBy;
		UBaseWidgetBlueprint* WidgetBlueprint = Cast<UBaseWidgetBlueprint>(Blueprint);
		return WidgetBlueprint
			? WidgetBlueprint->WidgetTree
			: nullptr;
	}

	void FWidgetConnectionConfigTypeCustomization::CustomizeConnectionTargetsReferenceProperty(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		TSharedRef<IPropertyHandle> ConnectionTargetsPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils
		) const
	{
		ConnectionTargetsPropertyHandle->MarkHiddenByCustomization();
		const TSharedRef<FConnectionTargetNodeBuilder> CustomBuilder = MakeShared<FConnectionTargetNodeBuilder>(
			ConnectionTargetsPropertyHandle,
			CreateGetConnectionsFromChildWidgetAttribute(StructPropertyHandle, CustomizationUtils),
			CustomizationUtils
			);
		ChildBuilder.AddCustomBuilder(CustomBuilder);
	}

	TAttribute<TArray<FName>> FWidgetConnectionConfigTypeCustomization::CreateGetConnectionsFromChildWidgetAttribute(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IPropertyTypeCustomizationUtils& CustomizationUtils
		) const
	{
		const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = CustomizationUtils.GetPropertyUtilities()->GetSelectedObjects();
		if (SelectedObjects.Num() != 1 || !SelectedObjects[0].IsValid())
		{
			return TArray<FName>{};
		}
		
		UObject* EditedObject = SelectedObjects[0].Get();
		UVCamStateSwitcherWidget* StateSwitcherWidget = Cast<UVCamStateSwitcherWidget>(EditedObject);
		if (!StateSwitcherWidget)
		{
			UE_LOG(LogVCamEditor, Error, TEXT("FWidgetConnectionConfig was expected to be within an UVCamStateSwitcherWidget object!"));
			return TArray<FName>{};
		}

		return TAttribute<TArray<FName>>::CreateLambda([WeakStateSwitcher = TWeakObjectPtr<UVCamStateSwitcherWidget>(StateSwitcherWidget), StructPropertyHandle]() -> TArray<FName>
		{
			if (!WeakStateSwitcher.IsValid())
			{
				return {};
			}
			
			void* Data;
			const FPropertyAccess::Result AccessResult = StructPropertyHandle->GetValueData(Data);
			if (AccessResult != FPropertyAccess::Success)
			{
				return {};
			}

			FWidgetConnectionConfig* ConfigData = reinterpret_cast<FWidgetConnectionConfig*>(Data);
			const UVCamWidget* VCamWidget = ConfigData->ResolveWidget(WeakStateSwitcher.Get());
			if (!VCamWidget)
			{
				return {};
			}

			TArray<FName> Result;
			VCamWidget->Connections.GenerateKeyArray(Result);
			return Result;
		});
	}
}

#undef LOCTEXT_NAMESPACE