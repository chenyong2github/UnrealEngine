// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMViewModelPanel.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/MVVMEditorStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/PropertyViewer/SPropertyViewer.h"
#include "Widgets/SMVVMSelectViewModel.h"

#include "SPositiveActionButton.h"

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
	FieldIterator = MakeUnique<FFieldIterator_Bindable>(WidgetBlueprint, EFieldVisibility::All);

	if (CurrentBlueprintView)
	{
		// Listen to when the viewmodel are modified
		ViewModelsUpdatedHandle = CurrentBlueprintView->OnViewModelsUpdated.AddSP(this, &SMVVMViewModelPanel::HandleViewModelsUpdated);
	}

	ViewModelTreeView = SNew(UE::PropertyViewer::SPropertyViewer)
		.PropertyVisibility(UE::PropertyViewer::SPropertyViewer::EPropertyVisibility::Visible)
		.bShowSearchBox(true)
		.bShowFieldIcon(true)
		.bSanitizeName(true)
		.FieldIterator(FieldIterator.Get())
		.SearchBoxPreSlot()
		[
			SAssignNew(AddMenuButton, SPositiveActionButton)
			.OnGetMenuContent(this, &SMVVMViewModelPanel::MakeAddMenu)
			.Text(LOCTEXT("Viewmodel", "Viewmodel"))
			.IsEnabled(this, &SMVVMViewModelPanel::HandleCanEditViewmodelList)
		]
		;
		//.OnRenamed(this, &SMVVMViewModelPanel::HandleViewModelRenamed);

	FillViewModel();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.Padding(0)
		[
			ViewModelTreeView.ToSharedRef()
		]
	];
}


SMVVMViewModelPanel::~SMVVMViewModelPanel()
{
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
		CurrentBlueprintView->OnViewModelsUpdated.Remove(ViewModelsUpdatedHandle);
	}
}


void SMVVMViewModelPanel::FillViewModel()
{
	if (ViewModelTreeView)
	{
		ViewModelTreeView->RemoveAll();

		if (UMVVMBlueprintView* View = WeakBlueprintView.Get())
		{
			for (const FMVVMBlueprintViewModelContext& ViewModelContext : View->GetViewModels())
			{
				if (ViewModelContext.GetViewModelClass())
				{
					UObject* ViewModelInstance = ViewModelContext.GetViewModelClass()->GetDefaultObject();
					ViewModelTreeView->AddInstance(ViewModelInstance, ViewModelContext.GetDisplayName());
				}
				else
				{
					// Find a way to show context that are not valid anymore
					//ViewModelHandle.Value = ViewModelTreeView->AddInstance(ViewModelInstance, ViewModelContext.GetDisplayName());
				}
			}
		}
	}
}


void SMVVMViewModelPanel::HandleViewUpdated(UBlueprintExtension*)
{
	bool bViewUpdated = false;

	if (!ViewModelsUpdatedHandle.IsValid())
	{
		if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
		{
			if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj())
			{
				UMVVMBlueprintView* CurrentBlueprintView = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->GetView(WidgetBlueprint);
				WeakBlueprintView = CurrentBlueprintView;

				if (CurrentBlueprintView)
				{
					ViewModelsUpdatedHandle = CurrentBlueprintView->OnViewModelsUpdated.AddSP(this, &SMVVMViewModelPanel::HandleViewModelsUpdated);
					bViewUpdated = true;
				}
			}
		}
	}

	if (bViewUpdated)
	{
		FillViewModel();
	}
}


void SMVVMViewModelPanel::HandleViewModelsUpdated()
{
	FillViewModel();
}


TSharedRef<SWidget> SMVVMViewModelPanel::MakeAddMenu()
{
	const UWidgetBlueprint* WidgetBlueprint = nullptr;
	if (TSharedPtr<FWidgetBlueprintEditor> EditorPin = WeakBlueprintEditor.Pin())
	{
		WidgetBlueprint = EditorPin->GetWidgetBlueprintObj();
	}
	return SNew(SBox)
		.WidthOverride(600)
		.HeightOverride(500)
		[
			SNew(SMVVMSelectViewModel, WidgetBlueprint)
			.OnCancel(this, &SMVVMViewModelPanel::HandleCancelAddMenu)
			.OnViewModelCommitted(this, &SMVVMViewModelPanel::HandleAddMenuViewModel)
		];
}


void SMVVMViewModelPanel::HandleCancelAddMenu()
{
	if (AddMenuButton)
	{
		AddMenuButton->SetIsMenuOpen(false, false);
	}
}

void SMVVMViewModelPanel::HandleAddMenuViewModel(const UClass* SelectedClass)
{
	if (AddMenuButton)
	{
		AddMenuButton->SetIsMenuOpen(false, false);
		if (SelectedClass)
		{
			if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
			{
				if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj())
				{
					UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
					check(EditorSubsystem);
					EditorSubsystem->AddViewModel(WidgetBlueprint, SelectedClass);
				}
			}
		}
	}
}


bool SMVVMViewModelPanel::HandleCanEditViewmodelList() const
{
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
	{
		return WidgetBlueprintEditor->InEditingMode();
	}
	return false;
}

} // namespace

#undef LOCTEXT_NAMESPACE