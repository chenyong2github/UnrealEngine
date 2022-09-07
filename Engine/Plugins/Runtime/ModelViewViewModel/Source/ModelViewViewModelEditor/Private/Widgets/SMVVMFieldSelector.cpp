// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMFieldSelector.h"
 
#include "ClassViewerModule.h"
#include "Editor.h"
#include "Hierarchy/SReadOnlyHierarchyView.h"
#include "Modules/ModuleManager.h"
#include "MVVMEditorSubsystem.h"
#include "SNegativeActionButton.h"
#include "SPositiveActionButton.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "Styling/MVVMEditorStyle.h"
#include "Styling/SlateIconFinder.h"
#include "WidgetBlueprint.h"
#include "Widgets/SMVVMFieldEntry.h"
#include "Widgets/SMVVMSourceEntry.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "MVVMFieldSelector"

namespace UE::MVVM
{

namespace Private
{

FBindingSource GetSourceFromPath(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintPropertyPath& Path)
{
	if (Path.IsFromViewModel())
	{
		return FBindingSource::CreateForViewModel(WidgetBlueprint, Path.GetViewModelId());
	}
	else if (Path.IsFromWidget())
	{
		return FBindingSource::CreateForWidget(WidgetBlueprint, Path.GetWidgetName());
	}
	return FBindingSource();
}

} // namespace Private

void SFieldSelector::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint, bool bInViewModelProperty)
{
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	check(OnSelectionChangedDelegate.IsBound());

	WidgetBlueprint = InWidgetBlueprint;
	check(WidgetBlueprint);

	SelectedField = InArgs._SelectedField;
	check(SelectedField.IsSet());
	CachedSelectedField = SelectedField.Get();

	BindingMode = InArgs._BindingMode;
	check(BindingMode.IsSet());

	bViewModelProperty = bInViewModelProperty;

	AssignableTo = InArgs._AssignableTo;

	TextStyle = InArgs._TextStyle;

	TSharedPtr<SHorizontalBox> HBox;

	ChildSlot
	[
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(FMVVMEditorStyle::Get(), "FieldSelector.ComboButton")
		.OnGetMenuContent(this, &SFieldSelector::OnGetMenuContent)
		.ContentPadding(FMargin(4, 2))
		.ButtonContent()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(HBox, SHorizontalBox)
				.Visibility_Lambda([this]() { return CachedSelectedField.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					
			]
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Padding(FMargin(8, 0, 8, 0))
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]() { return CachedSelectedField.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					.TextStyle(FAppStyle::Get(), "HintText")
					.Text(LOCTEXT("None", "No field selected"))
				]
			]
		]
	];
	
	if (InArgs._ShowSource)
	{
		HBox->AddSlot()
			.Padding(8, 0, 0, 0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SAssignNew(SelectedSourceWidget, SSourceEntry)
			];

		HBox->AddSlot()
			.Padding(8, 0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.ChevronRight"))
			];					
	}
	else
	{
		FixedSource = InArgs._Source;
	}

	HBox->AddSlot()
		.Padding(0, 0, 8, 0)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SAssignNew(SelectedEntryWidget, SFieldEntry)
			.TextStyle(TextStyle)
		];

	Refresh();
}

void SFieldSelector::Refresh()
{
	CachedSelectedField = SelectedField.Get();

	if (SelectedEntryWidget.IsValid())
	{
		SelectedEntryWidget->SetField(CachedSelectedField);
	}

	if (SelectedSourceWidget.IsValid())
	{
		SelectedSourceWidget->RefreshSource(Private::GetSourceFromPath(WidgetBlueprint, CachedSelectedField));
	}
}

TSharedRef<SWidget> SFieldSelector::OnGenerateFieldWidget(FMVVMBlueprintPropertyPath Path) const
{
	return SNew(SFieldEntry)
		.TextStyle(TextStyle)
		.Field(Path);
}

bool SFieldSelector::IsSelectEnabled() const
{
	if (BindingList.IsValid())
	{
		FMVVMBlueprintPropertyPath Path = BindingList->GetSelectedProperty();
		return (Path.IsFromViewModel() || Path.IsFromWidget()) && !Path.GetBasePropertyPath().IsEmpty();
	}
	return false;
}

FReply SFieldSelector::OnSelectProperty()
{
	if (BindingList.IsValid())
	{
		FMVVMBlueprintPropertyPath Path = BindingList->GetSelectedProperty();
		SetSelection(Path);
	}
	return FReply::Handled();
}

bool SFieldSelector::IsClearEnabled() const
{
	return !CachedSelectedField.IsEmpty();
}

FReply SFieldSelector::OnClearBinding()
{
	SetSelection(FMVVMBlueprintPropertyPath());
	return FReply::Handled();
}

FReply SFieldSelector::OnCancel()
{
	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}

	return FReply::Handled();
}

void SFieldSelector::SetSelection(const FMVVMBlueprintPropertyPath& SelectedPath)
{
	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}

	OnSelectionChangedDelegate.Execute(SelectedPath);
	Refresh();
}

void SFieldSelector::OnSearchTextChanged(const FText& NewText)
{
	BindingList->SetRawFilterText(NewText);
}

void SFieldSelector::OnViewModelSelected(FBindingSource Source, ESelectInfo::Type)
{
	if (BindingList.IsValid())
	{
		TArray<FBindingSource> Selection = ViewModelList->GetSelectedItems();

		BindingList->Clear();
		BindingList->AddSources(Selection);
	}
}

void SFieldSelector::OnWidgetSelected(FName WidgetName, ESelectInfo::Type)
{
	if (BindingList.IsValid())
	{
		FBindingSource Source = FBindingSource::CreateForWidget(WidgetBlueprint, WidgetName);

		BindingList->Clear();
		BindingList->AddSource(Source);
	}
}

TSharedRef<ITableRow> SFieldSelector::GenerateRowForViewModel(FBindingSource ViewModel, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(STableRow<FBindingSource>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FSlateIconFinder::FindIconBrushForClass(ViewModel.Class))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(ViewModel.DisplayName)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

TSharedRef<SWidget> SFieldSelector::OnGetMenuContent()
{
	ViewModelSources.Reset();
	ViewModelList.Reset();

	BindingList = SNew(SSourceBindingList, WidgetBlueprint)
		.ShowSearchBox(false)
		.OnDoubleClicked(this, &SFieldSelector::SetSelection)
		.FieldVisibilityFlags(GetFieldVisibilityFlags())
		.AssignableTo(AssignableTo);

	TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(0, 4, 0, 4)
		.AutoHeight()
		[
			SNew(SSearchBox)
			.OnTextChanged(this, &SFieldSelector::OnSearchTextChanged)
		];

	// single fixed source
	if (FixedSource.IsSet())
	{
		BindingList->AddSource(FixedSource.GetValue());

		VBox->AddSlot()
			[
				BindingList.ToSharedRef()
			];
	}
	else
	{ 
		// show source picker
		TSharedPtr<SWidget> SourcePicker;
		if (bViewModelProperty)
		{
			FBindingSource SelectedSource;

			UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			if (const UMVVMBlueprintView* View = EditorSubsystem->GetView(WidgetBlueprint))
			{
				const TArrayView<const FMVVMBlueprintViewModelContext> ViewModels = View->GetViewModels();
				ViewModelSources.Reserve(ViewModels.Num());

				for (const FMVVMBlueprintViewModelContext& ViewModel : ViewModels)
				{
					FBindingSource& Source = ViewModelSources.AddDefaulted_GetRef();
					Source.Class = ViewModel.GetViewModelClass();
					Source.Name = ViewModel.GetViewModelName();
					Source.DisplayName = ViewModel.GetDisplayName();
					Source.ViewModelId = ViewModel.GetViewModelId();

					if (Source.ViewModelId == CachedSelectedField.GetViewModelId())
					{
						SelectedSource = Source;
					}
				}
			}

			ViewModelList = SNew(SListView<FBindingSource>)
				.OnGenerateRow(this, &SFieldSelector::GenerateRowForViewModel)
				.SelectionMode(ESelectionMode::Multi)
				.ListItemsSource(&ViewModelSources)
				.OnSelectionChanged(this, &SFieldSelector::OnViewModelSelected);

			ViewModelList->SetItemSelection(SelectedSource, true);

			SourcePicker = ViewModelList;
		}
		else
		{
			WidgetList = SNew(SReadOnlyHierarchyView, WidgetBlueprint)
				.OnSelectionChanged(this, &SFieldSelector::OnWidgetSelected)
				.SelectionMode(ESelectionMode::Multi)
				.ShowSearch(false);

			SourcePicker = WidgetList;
		}

		VBox->AddSlot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(4.0f)
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SourcePicker.ToSharedRef()
				]
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					BindingList.ToSharedRef()
				]
			];
	}
				
	VBox->AddSlot()
		.Padding(4, 4, 4, 0)
		.HAlign(HAlign_Right)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SPrimaryButton)
				.OnClicked(this, &SFieldSelector::OnSelectProperty)
				.IsEnabled(this, &SFieldSelector::IsSelectEnabled)
				.Text(LOCTEXT("Select", "Select"))
			]
			+ SHorizontalBox::Slot()
			.Padding(FAppStyle::Get().GetMargin("StandardDialog.SlotPadding"))
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SFieldSelector::OnClearBinding)
				.IsEnabled(this, &SFieldSelector::IsClearEnabled)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Clear", "Clear"))
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(FAppStyle::Get().GetMargin("StandardDialog.SlotPadding"))
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SFieldSelector::OnCancel)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Cancel", "Cancel"))
				]
			]
		];

	TSharedRef<SWidget> MenuWidget = 
		SNew(SBox)
		.MinDesiredWidth(400)
		.MinDesiredHeight(200)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8, 2, 8, 3))
			[
				VBox
			]
		];

	return MenuWidget;
}

EFieldVisibility SFieldSelector::GetFieldVisibilityFlags() const
{
	EFieldVisibility Flags = EFieldVisibility::None;

	EMVVMBindingMode Mode = BindingMode.Get();
	if (bViewModelProperty)
	{
		if (IsForwardBinding(Mode))
		{
			Flags |= EFieldVisibility::Readable;

			if (!IsOneTimeBinding(Mode))
			{
				Flags |= EFieldVisibility::Notify;
			}
		}
		if (IsBackwardBinding(Mode))
		{
			Flags |= EFieldVisibility::Writable;
		}
	}
	else
	{
		if (IsForwardBinding(Mode))
		{
			Flags |= EFieldVisibility::Writable;
		}
		if (IsBackwardBinding(Mode))
		{
			Flags |= EFieldVisibility::Readable;

			if (!IsOneTimeBinding(Mode))
			{
				Flags |= EFieldVisibility::Notify;
			}
		}
	}

	return Flags;
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
