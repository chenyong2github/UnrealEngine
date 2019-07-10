// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityWidgetBlueprintFactory.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorUtilityBlueprint.h"
#include "GlobalEditorUtilityBase.h"
#include "PlacedEditorUtilityBase.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/SClassPickerDialog.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Components/CanvasPanel.h"
#include "BaseWidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/GridPanel.h"
#include "UMGEditorProjectSettings.h"

#define LOCTEXT_NAMESPACE "UEditorUtilityWidgetBlueprintFactory"

class FEditorUtilityWidgetBlueprintFactoryFilter : public IClassViewerFilter
{
public:
	/** All children of these classes will be included unless filtered out by another setting. */
	TSet< const UClass* > AllowedChildrenOfClasses;

	/** Disallowed class flags. */
	EClassFlags DisallowedClassFlags;

	bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
	{
		return !InClass->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
	}
};

/////////////////////////////////////////////////////
// UEditorUtilityWidgetBlueprintFactory

UEditorUtilityWidgetBlueprintFactory::UEditorUtilityWidgetBlueprintFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UEditorUtilityWidgetBlueprint::StaticClass();
	ParentClass = UEditorUtilityWidget::StaticClass();
}

bool UEditorUtilityWidgetBlueprintFactory::ConfigureProperties()
{
	if (GetDefault<UUMGEditorProjectSettings>()->bUseWidgetTemplateSelector)
	{
		// Load the classviewer module to display a class picker
		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

		// Fill in options
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.bShowNoneOption = true;

		Options.ExtraPickerCommonClasses.Add(UHorizontalBox::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UVerticalBox::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UGridPanel::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UCanvasPanel::StaticClass());

		TSharedPtr<FEditorUtilityWidgetBlueprintFactoryFilter> Filter = MakeShareable(new FEditorUtilityWidgetBlueprintFactoryFilter);
		Options.ClassFilter = Filter;

		Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
		Filter->AllowedChildrenOfClasses.Add(UPanelWidget::StaticClass());

		const FText TitleText = LOCTEXT("CreateWidgetBlueprint", "Pick Root Widget for New Editor Utility Widget");
		return SClassPickerDialog::PickClass(TitleText, Options, RootWidgetClass, UPanelWidget::StaticClass());

	}
	return true;
}

UObject* UEditorUtilityWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// Make sure we are trying to factory a blueprint, then create and init one
	check(Class->IsChildOf(UEditorUtilityWidgetBlueprint::StaticClass()));

	FString ParentPath = InParent->GetPathName();

	if ((ParentClass == nullptr) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString(ParentClass->GetName()) : NSLOCTEXT("UnrealEd", "Null", "(null)"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("UnrealEd", "CannotCreateBlueprintFromClass", "Cannot create a blueprint based on the class '{0}'."), Args));
		return nullptr;
	}
	else
	{
		// If the root widget selection dialog is not enabled, use a canvas panel as the root by default
		if (!GetDefault<UUMGEditorProjectSettings>()->bUseWidgetTemplateSelector)
		{
			RootWidgetClass = UCanvasPanel::StaticClass();
		}
		UEditorUtilityWidgetBlueprint* NewBP = CastChecked<UEditorUtilityWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BlueprintType, UEditorUtilityWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), NAME_None));

		// Create the selected root widget
		if (NewBP->WidgetTree->RootWidget == nullptr)
		{
			if (TSubclassOf<UPanelWidget> RootWidgetPanel = RootWidgetClass)
			{
				UWidget* Root = NewBP->WidgetTree->ConstructWidget<UWidget>(RootWidgetPanel);
				NewBP->WidgetTree->RootWidget = Root;
			}
		}

		return NewBP;
	}
}

bool UEditorUtilityWidgetBlueprintFactory::CanCreateNew() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE