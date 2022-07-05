// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMFieldSelector.h"
 
#include "Editor.h"
#include "MVVMEditorSubsystem.h"
#include "SNegativeActionButton.h"
#include "SPositiveActionButton.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "Styling/MVVMEditorStyle.h"
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
	OnValidateFieldDelegate = InArgs._OnValidateField;
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	check(OnSelectionChangedDelegate.IsBound());
	WidgetBlueprint = InWidgetBlueprint;
	check(WidgetBlueprint);

	SelectedField = InArgs._SelectedField;
	check(SelectedField.IsSet());

	BindingMode = InArgs._BindingMode;
	check(BindingMode.IsSet());

	bViewModelProperty = bInViewModelProperty;

	TextStyle = InArgs._TextStyle;

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
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
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return CachedSelectedField.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					+ SHorizontalBox::Slot()
					.Padding(8, 0, 0, 0)
					.AutoWidth()
					[
						SAssignNew(SelectedSourceWidget, SSourceEntry)
					]
					+ SHorizontalBox::Slot()
					.Padding(8, 0)
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.ChevronRight"))
					]
					+ SHorizontalBox::Slot()
					.Padding(0, 0, 8, 0)
					.AutoWidth()
					[
						SAssignNew(SelectedEntryWidget, SFieldEntry)
						.TextStyle(TextStyle)
						.OnValidateField(this, &SFieldSelector::ValidateField)
					]
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
		]
	];

	Refresh();
}

void SFieldSelector::OnFieldSelected(FMVVMBlueprintPropertyPath Selected)
{
	OnSelectionChangedDelegate.ExecuteIfBound(Selected);
	
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

TValueOrError<bool, FString> SFieldSelector::ValidateField(FMVVMBlueprintPropertyPath Field) const
{
	if (OnValidateFieldDelegate.IsBound())
	{
		return OnValidateFieldDelegate.Execute(Field);
	}

	return MakeValue(true);
}

TSharedRef<SWidget> SFieldSelector::OnGenerateFieldWidget(FMVVMBlueprintPropertyPath Path) const
{
	return SNew(SFieldEntry)
		.TextStyle(TextStyle)
		.Field(Path)
		.OnValidateField(this, &SFieldSelector::ValidateField);
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

void SFieldSelector::HandleSearchChanged(const FText& InFilterText)
{
	BindingList->SetRawFilterText(InFilterText);
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

TSharedRef<SWidget> SFieldSelector::OnGetMenuContent()
{
	TSharedRef<SWidget> MenuWidget = 
		SNew(SBorder)
		.BorderImage(FMVVMEditorStyle::Get().GetBrush("FieldSelector.MenuBorderBrush"))
		.Padding(FMargin(8, 1, 8, 3))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchViewmodel", "Search"))
				.OnTextChanged(this, &SFieldSelector::HandleSearchChanged)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Dropdown"))
				.Padding(0)
				[
					SAssignNew(BindingList, SSourceBindingList, WidgetBlueprint)
					.OnDoubleClicked(this, &SFieldSelector::SetSelection)
					.FieldVisibilityFlags(GetFieldVisibilityFlags())
				]
			]
			+ SVerticalBox::Slot()
			.Padding(4, 4, 4, 0)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &SFieldSelector::OnCancel)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Cancel", "Cancel"))
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.OnClicked(this, &SFieldSelector::OnClearBinding)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Clear", "Clear"))
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(4, 0, 0, 0)
				[
					SNew(SPrimaryButton)
					.OnClicked(this, &SFieldSelector::OnSelectProperty)
					.Text(LOCTEXT("Select", "Select"))
				]
			]
		];

	UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
	if (bViewModelProperty)
	{
		if (UMVVMBlueprintView* View = EditorSubsystem->GetView(WidgetBlueprint))
		{
			BindingList->AddViewModels(View->GetViewModels());
		}
	}
	else
	{
		TArray<FBindingSource> BindableWidgets = EditorSubsystem->GetBindableWidgets(WidgetBlueprint);
		BindingList->AddSources(BindableWidgets);
	}

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
