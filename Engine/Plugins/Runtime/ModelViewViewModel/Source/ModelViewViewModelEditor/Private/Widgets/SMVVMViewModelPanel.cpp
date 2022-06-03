// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMViewModelPanel.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMViewModelBase.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/MVVMEditorStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SMVVMViewModelBindingListWidget.h"

#define LOCTEXT_NAMESPACE "ViewModelPanel"

namespace UE::MVVM
{

void SMVVMViewModelPanel::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor)
{
	UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj();
	check(WidgetBlueprint);
	UMVVMBlueprintView* CurrentBlueprintView = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->GetView(WidgetBlueprint);

	WeakBlueprintEditor = WidgetBlueprintEditor;
	WeakBlueprintView = CurrentBlueprintView;

	WidgetBlueprint->OnExtensionAdded.AddSP(this, &SMVVMViewModelPanel::HandleViewUpdated);
	WidgetBlueprint->OnExtensionRemoved.AddSP(this, &SMVVMViewModelPanel::HandleViewUpdated);

	if (CurrentBlueprintView)
	{
		// Listen to when the viewmodel are modified
		CurrentBlueprintView->OnViewModelsUpdated.AddSP(this, &SMVVMViewModelPanel::HandleViewModelsUpdated);
	}

	GEditor->OnBlueprintCompiled().AddSP(this, &SMVVMViewModelPanel::HandleBlueprintCompiled);

	ViewModelTreeView = SNew(SViewModelBindingListWidget)
		.OnRenamed(this, &SMVVMViewModelPanel::HandleViewModelRenamed);

	GenerateViewModelTreeView();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew(SearchBoxPtr, SSearchBox)
			.HintText(LOCTEXT("SearchViewmodel", "Search"))
			.OnTextChanged(this, &SMVVMViewModelPanel::HandleSearchChanged)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
			.Padding(0)
			[
				ViewModelTreeView.ToSharedRef()
			]
		]
	];
}


SMVVMViewModelPanel::~SMVVMViewModelPanel()
{
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().RemoveAll(this);
	}

	if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
	{
		if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj())
		{
			WidgetBlueprint->OnExtensionAdded.RemoveAll(this);
			WidgetBlueprint->OnExtensionRemoved.RemoveAll(this);
		}
	}

	if (UMVVMBlueprintView* CurrentBlueprintView = WeakBlueprintView.Get())
	{
		// bind to check if the view is enabled
		CurrentBlueprintView->OnViewModelsUpdated.RemoveAll(this);
	}
}


void SMVVMViewModelPanel::GenerateViewModelTreeView()
{
	if (ViewModelTreeView)
	{
		TArray<SViewModelBindingListWidget::FViewModel, TInlineAllocator<16>> ViewModelList;
		if (UMVVMBlueprintView* View = WeakBlueprintView.Get())
		{
			for (const FMVVMBlueprintViewModelContext& ViewModelContext : View->GetViewModels())
			{
				SViewModelBindingListWidget::FViewModel ViewModel;
				ViewModel.Class = ViewModelContext.GetViewModelClass();
				ViewModel.ViewModelId = ViewModelContext.GetViewModelId();
				ViewModel.Name = ViewModelContext.GetViewModelName();
				ViewModelList.Add(ViewModel);
			}
		}

		ViewModelTreeView->SetViewModels(ViewModelList);
	}
}


void SMVVMViewModelPanel::HandleViewUpdated(UBlueprintExtension*)
{
	bool bViewUpdated = false;
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
	{
		if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj())
		{
			UMVVMBlueprintView* CurrentBlueprintView = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->GetView(WidgetBlueprint);
			WeakBlueprintView = CurrentBlueprintView;

			if (CurrentBlueprintView)
			{
				CurrentBlueprintView->OnViewModelsUpdated.AddSP(this, &SMVVMViewModelPanel::HandleViewModelsUpdated);
			}
		}
	}
	GenerateViewModelTreeView();
}


void SMVVMViewModelPanel::HandleViewModelsUpdated()
{
	GenerateViewModelTreeView();
}


void SMVVMViewModelPanel::HandleSearchChanged(const FText& InFilterText)
{
	SearchBoxPtr->SetError(ViewModelTreeView->SetRawFilterText(InFilterText));
}


bool SMVVMViewModelPanel::HandleViewModelRenamed(const SViewModelBindingListWidget::FViewModel& ViewModel, const FText& RenameTo, bool bCommit, FText& OutErrorMessage)
{
	if (RenameTo.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("EmptyWidgetName", "Empty Widget Name");
		return false;
	}

	const FString& NewNameString = RenameTo.ToString();
	if (NewNameString.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("WidgetNameTooLong", "Widget Name is Too Long");
		return false;
	}

	FString GeneratedName = SlugStringForValidName(NewNameString);
	if (GeneratedName.IsEmpty())
	{
		OutErrorMessage = LOCTEXT("EmptyWidgetName", "Empty Widget Name");
		return false;
	}

	const FName GeneratedFName(*GeneratedName);
	check(GeneratedFName.IsValidXName(INVALID_OBJECTNAME_CHARACTERS));

	if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
	{
		if (UWidgetBlueprint* WidgetBP = WidgetBlueprintEditor->GetWidgetBlueprintObj())
		{
			if (bCommit)
			{
				return GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->RenameViewModel(WidgetBP, ViewModel.Name, *NewNameString, OutErrorMessage);
			}
			else
			{
				return GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->VerifyViewModelRename(WidgetBP, ViewModel.Name, *NewNameString, OutErrorMessage);
			}
		}
	}
	return false;
}


void SMVVMViewModelPanel::HandleBlueprintCompiled()
{
	GenerateViewModelTreeView();
}

} // namespace

#undef LOCTEXT_NAMESPACE