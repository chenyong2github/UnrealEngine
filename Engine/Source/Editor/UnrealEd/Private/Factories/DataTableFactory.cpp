// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/DataTableFactory.h"
#include "DataTableEditorUtils.h"
#include "Engine/DataTable.h"
#include "Editor.h"

#include "Modules/ModuleManager.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"

#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "Input/Reply.h"

#define LOCTEXT_NAMESPACE "DataTableFactory"

UDataTableFactory::UDataTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UDataTable::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UDataTableFactory::ConfigureProperties()
{
	class FDataTableStructFilter : public IStructViewerFilter
	{
	public:
		virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			return FDataTableEditorUtils::IsValidTableStruct(InStruct);
		}

		virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			// Unloaded structs are always User Defined Structs, and User Defined Structs are always allowed
			// They will be re-validated by IsStructAllowed once loaded during the pick
			return true;
		}
	};

	class FDataTableClassFilter : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs ) override
		{
			return InClass && !InClass->HasAnyClassFlags(EClassFlags::CLASS_Abstract) && InClass->IsChildOf(UDataTable::StaticClass());
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const class IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs) override
		{
			// DataTable cannot have Blueprint sub-classes.
			return false;
		}
	};

	class FDataTableFactoryUI : public TSharedFromThis<FDataTableFactoryUI>
	{
	public:
		FReply OnCreate()
		{
			check(ResultStruct);
			check(ResultClass);

			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
			return FReply::Handled();
		}

		FReply OnCancel()
		{
			ResultStruct = nullptr;
			ResultClass = nullptr;

			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
			return FReply::Handled();
		}

		bool IsValidSelection() const
		{
			return ResultStruct && ResultClass;
		}

		void OnPickedStruct(const UScriptStruct* ChosenStruct)
		{
			ResultStruct = ChosenStruct;
			StructPickerAnchor->SetIsOpen(false);
		}

		void OnPickedClass(UClass* ChosenClass)
		{
			ResultClass = ChosenClass;
			ClassPickerAnchor->SetIsOpen(false);
		}

		FText OnGetStructComboTextValue() const
		{
			return ResultStruct
				? FText::AsCultureInvariant(ResultStruct->GetName())
				: LOCTEXT("None", "None");
		}

		FText OnGetClassComboTextValue() const
		{
			return ResultClass
				? FText::AsCultureInvariant(ResultClass->GetName())
				: LOCTEXT("None", "None");
		}

		TSharedRef<SWidget> GenerateStructPicker()
		{
			FStructViewerModule& StructViewerModule = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer");

			// Fill in options
			FStructViewerInitializationOptions Options;
			Options.Mode = EStructViewerMode::StructPicker;
			Options.StructFilter = MakeShared<FDataTableStructFilter>();

			return
				SNew(SBox)
				.WidthOverride(330)
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.FillHeight(1.0f)
					.MaxHeight(500)
					[
						SNew(SBorder)
						.Padding(4)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							StructViewerModule.CreateStructViewer(Options, FOnStructPicked::CreateSP(this, &FDataTableFactoryUI::OnPickedStruct))
						]
					]
				];
		}

		TSharedRef<SWidget> GenerateClassPicker()
		{
			FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

			FClassViewerInitializationOptions Options;
			Options.Mode = EClassViewerMode::ClassPicker;
			Options.ClassFilters.Add(MakeShared<FDataTableClassFilter>());
			Options.bShowNoneOption = false;

			return SNew(SBox)
				.WidthOverride(330)
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.FillHeight(1.0f)
					.MaxHeight(500)
					[
						SNew(SBorder)
						.Padding(4)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &FDataTableFactoryUI::OnPickedClass))
						]
					]
				];
		}

		bool OpenSelectorDialog(TObjectPtr<const UClass> ClassResult, TObjectPtr<const UScriptStruct> StructResult)
		{
			PickerWindow = SNew(SWindow)
				.Title(LOCTEXT("DataTableFactoryOptions", "Pick Class & Row Structure"))
				.ClientSize(FVector2D(350, 100))
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
					.Padding(10)
					[
						SNew(SVerticalBox)
						// Class Picker
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(ClassPickerAnchor, SComboButton)
							.ContentPadding(FMargin(2,2,2,1))
							.MenuPlacement(MenuPlacement_BelowAnchor)
							.ButtonContent()
							[
								SNew(STextBlock)
								.Text(this, &FDataTableFactoryUI::OnGetClassComboTextValue)
							]
							.OnGetMenuContent(this, &FDataTableFactoryUI::GenerateClassPicker)
						]
						// Struct Picker
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(StructPickerAnchor, SComboButton)
							.ContentPadding(FMargin(2,2,2,1))
							.MenuPlacement(MenuPlacement_BelowAnchor)
							.ButtonContent()
							[
								SNew(STextBlock)
								.Text(this, &FDataTableFactoryUI::OnGetStructComboTextValue)
							]
							.OnGetMenuContent(this, &FDataTableFactoryUI::GenerateStructPicker)
						]
						+SVerticalBox::Slot()
						.HAlign(HAlign_Right)
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("OK", "OK"))
								.IsEnabled(this, &FDataTableFactoryUI::IsValidSelection)
								.OnClicked(this, &FDataTableFactoryUI::OnCreate)
							]
							+SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("Cancel", "Cancel"))
								.OnClicked(this, &FDataTableFactoryUI::OnCancel)
							]
						]
					]
				];

			GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
			PickerWindow.Reset();

			ClassResult = ResultClass;
			StructResult = ResultStruct;

			return IsValidSelection();
		}

	private:
		TSharedPtr<SWindow> PickerWindow;
		TSharedPtr<SComboButton> StructPickerAnchor;
		TSharedPtr<SComboButton> ClassPickerAnchor;
		const UScriptStruct* ResultStruct = nullptr;
		const UClass* ResultClass = UDataTable::StaticClass();
	};

	FDataTableFactoryUI ConfigSelector = FDataTableFactoryUI();

	TableClass = nullptr;
	Struct = nullptr;
	return ConfigSelector.OpenSelectorDialog(TableClass, Struct);
}

UObject* UDataTableFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDataTable* DataTable = nullptr;
	if (Struct && TableClass && (SupportedClass == Class))
	{
		ensure(0 != (RF_Public & Flags));
		DataTable = MakeNewDataTable(InParent, Name, Flags);
		if (DataTable)
		{
			DataTable->RowStruct = const_cast<UScriptStruct*>(ToRawPtr(Struct));
		}
	}
	return DataTable;
}

UDataTable* UDataTableFactory::MakeNewDataTable(UObject* InParent, FName Name, EObjectFlags Flags)
{
	return NewObject<UDataTable>(InParent, TableClass, Name, Flags);
}

#undef LOCTEXT_NAMESPACE // "DataTableFactory"
