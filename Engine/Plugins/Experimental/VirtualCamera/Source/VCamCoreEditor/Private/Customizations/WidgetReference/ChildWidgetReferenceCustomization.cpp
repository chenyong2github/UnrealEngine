// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChildWidgetReferenceCustomization.h"

#include "Customizations/StateSwitcher/SStringSelectionComboBox.h"
#include "Util/WidgetReference.h"
#include "Util/WidgetTreeUtils.h"

#include "Algo/AnyOf.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FWidgetReferenceForBlueprintCustomization"

namespace UE::VCamCoreEditor::Private
{
	TSharedRef<IPropertyTypeCustomization> FChildWidgetReferenceCustomization::MakeInstance()
	{
		return MakeShared<FChildWidgetReferenceCustomization>();
	}

	void FChildWidgetReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		const TSharedPtr<IPropertyHandle> TemplateProperty = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FChildWidgetReference, Template));
		
		const TArray<TWeakObjectPtr<UObject>>& EditedObjects = CustomizationUtils.GetPropertyUtilities()->GetSelectedObjects();
		TWeakObjectPtr<UUserWidget> EditedWidget = EditedObjects.Num() == 1 ? Cast<UUserWidget>(EditedObjects[0]) : nullptr;
		// Always only search on the template
		if (EditedWidget.IsValid() && !EditedWidget->HasAnyFlags(RF_ClassDefaultObject))
		{
			UObject* DefaultObject = EditedWidget->GetClass()->GetDefaultObject();
			EditedWidget = Cast<UUserWidget>(DefaultObject);
		}
		
		if (!EditedWidget.IsValid() || !ensure(TemplateProperty))
		{
			const FText Text = LOCTEXT("NotEditable", "No editable in this context");
			HeaderRow.
				NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				[
					SNew(STextBlock)
					.Text(Text)
					.ToolTipText(Text)
					.Font(CustomizationUtils.GetRegularFont())
				];
			return;
		}
		
		HeaderRow.
			NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SStringSelectionComboBox)
				.ToolTipText(LOCTEXT("PropertySelection.Tooltip", "Select a child widget from this Blueprint.\n\nThis can either be:\n\t- an auto-generated variable (see \"Is Variable\" check box in Designer)\n\t- a regular object property of type VCamWidget (you must make sure the property points to something valid before you change states)"))
				.SelectedItem_Lambda([TemplateProperty]()
				{
					UObject* Object = nullptr;
					TemplateProperty->GetValue(Object);
					return Object ? Object->GetName() : TEXT("None");
				})
				// Property list is only build once: when the Blueprint is recompiled,
				// the details view SHOULD refresh automatically and thus all of this is reconstructed. 
				.ItemList(this, &FChildWidgetReferenceCustomization::GetPropertyItemList, EditedWidget)
				.OnItemSelected_Lambda([this, TemplateProperty, EditedWidget](const FString& SelectedItem)
				{
					const TArray<UWidget*> Widgets = GetSelectableChildWidgets(EditedWidget);
					const FName WidgetName = *SelectedItem;
					UWidget* const* FoundWidget = Widgets.FindByPredicate([&WidgetName](UWidget* Widget){ return Widget->GetFName() == WidgetName; });
					
					// Setting null value is allowed (and even required)
					const FPropertyAccess::Result AccessResult = TemplateProperty->SetValue(FoundWidget ? *FoundWidget : nullptr);
					ensure(AccessResult == FPropertyAccess::Success);
				})
				.Font(CustomizationUtils.GetRegularFont())
			];
	}

	TArray<UWidget*> FChildWidgetReferenceCustomization::GetSelectableChildWidgets(TWeakObjectPtr<UUserWidget> Widget) const
	{
		if (!Widget.IsValid())
		{
			return {};
		}
		
		const UWidgetTree* WidgetTree = Widget->WidgetTree
			? Widget->WidgetTree.Get()
			: VCamCore::GetWidgetTreeThroughBlueprintAsset(*Widget);
		if (!WidgetTree)
		{
			return {};
		}
		
		TArray<UWidget*> Widgets;
		WidgetTree->ForEachWidget([&Widgets](UWidget* Widget)
		{
			if (UWidget* UserWidget = Cast<UWidget>(Widget))
			{
				Widgets.Add(UserWidget);
			}
		});
		return Widgets;
	}

	TArray<FString> FChildWidgetReferenceCustomization::GetPropertyItemList(TWeakObjectPtr<UUserWidget> Widget) const
	{
		TArray<FString> WidgetNamesAsStrings;
		Algo::Transform(GetSelectableChildWidgets(Widget), WidgetNamesAsStrings, [](UWidget* Widget){ return Widget->GetName(); });
		if (WidgetNamesAsStrings.Num() == 0)
		{
			WidgetNamesAsStrings.Add(TEXT("None"));
		}
		else
		{
			WidgetNamesAsStrings.Insert(TEXT("None"), 0);
		}
		return WidgetNamesAsStrings;
	}
}

#undef LOCTEXT_NAMESPACE
