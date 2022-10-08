// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooserWidgetFactories.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboButton.h"
#include "SClassViewer.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "ObjectChooserClassFilter.h"

#define LOCTEXT_NAMESPACE "DataInterfaceEditor"

namespace UE::ChooserEditor
{

void ConvertToText(UObject* Object, FText& OutText)
{
	UClass* Class = Object->GetClass();
	while (Class)
	{
		if (FChooserTextConverter* TextConverter = FObjectChooserWidgetFactories::ChooserTextConverter.Find(Class))
		{
			(*TextConverter)(Object, OutText);
			break;
		}
		Class = Class->GetSuperClass();
	}
}
	
TSharedPtr<SWidget> FObjectChooserWidgetFactories::CreateWidget(UObject* Value, UClass* ContextClass)
{
	if (Value)
	{
		UClass* Class = Value->GetClass();
		while (Class)
		{
			if (FChooserWidgetCreator* Creator = FObjectChooserWidgetFactories::ChooserWidgetCreators.Find(Class))
			{
				return (*Creator)(Value, ContextClass);
				break;
			}
			Class = Class->GetSuperClass();
		}
	}

	return nullptr;
}

TSharedPtr<SWidget> FObjectChooserWidgetFactories::CreateWidget(UClass* InterfaceType, UObject* Value, UClass* ContextClass, const FOnClassPicked& CreateClassCallback, TSharedPtr<SBorder>* InnerWidget)
{
	TSharedPtr<SWidget> LeftWidget = CreateWidget(Value, ContextClass);

	
	if (!LeftWidget.IsValid())
	{
		LeftWidget = SNew(STextBlock).Text(LOCTEXT("SelectDataType", "Select Data Type..."));
	}


	// button for replacing data with a different Data Interface class
	TSharedPtr<SComboButton> Button = SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton");
	
	Button->SetOnGetMenuContent(FOnGetContent::CreateLambda([InterfaceType, Button, CreateClassCallback]()
	{
		FClassViewerInitializationOptions Options;
		Options.ClassFilters.Add(MakeShared<FInterfaceClassFilter>(InterfaceType));
		
		// Add class filter for columns here
		TSharedRef<SWidget>  ClassMenu = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").
		CreateClassViewer(Options, FOnClassPicked::CreateLambda([Button, CreateClassCallback](UClass* Class)
		{
			Button->SetIsOpen(false);
			CreateClassCallback.Execute(Class);
		}));
		return ClassMenu;
	}));

	TSharedPtr <SBorder> Border;
	if (InnerWidget && InnerWidget->IsValid())
	{
		Border = *InnerWidget;
	}
	else
	{
		Border = SNew(SBorder);
	}
	
	if (InnerWidget)
	{
		*InnerWidget = Border;
	}
	
	Border->SetContent(LeftWidget.ToSharedRef());

	TSharedPtr<SWidget> Widget = SNew(SHorizontalBox)
		+SHorizontalBox::Slot().FillWidth(100)
		[
			Border.ToSharedRef()
		]
		+SHorizontalBox::Slot().AutoWidth()
		[
			Button.ToSharedRef()
		]
	;


	return Widget;
}

TMap<const UClass*, FChooserTextConverter> FObjectChooserWidgetFactories::ChooserTextConverter;
TMap<const UClass*, FChooserWidgetCreator> FObjectChooserWidgetFactories::ChooserWidgetCreators;

void ConvertToText_Base(const UObject* Object, FText& OutText)
{
	OutText = FText::FromString(Object->GetName());
}

void FObjectChooserWidgetFactories::RegisterWidgets()
{
	ChooserTextConverter.Add(UObject::StaticClass(), ConvertToText_Base);
}
	
}

#undef LOCTEXT_NAMESPACE
