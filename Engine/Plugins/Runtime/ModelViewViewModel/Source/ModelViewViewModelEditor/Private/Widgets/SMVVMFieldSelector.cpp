// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMFieldSelector.h"

#include "Algo/Transform.h"
#include "Engine/Engine.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "MVVMSubsystem.h"
#include "Widgets/Input/SComboBox.h"
#include "SSimpleButton.h"

#define LOCTEXT_NAMESPACE "MVVMFieldSelector"

using namespace UE::MVVM;

namespace UE::MVVM::Private
{
	FText GetFieldDisplayName(const FMVVMConstFieldVariant& Field)
	{
		if (Field.IsProperty())
		{
			return Field.GetProperty()->GetDisplayNameText();
		}
		else if (Field.IsFunction())
		{
			return Field.GetFunction()->GetDisplayNameText();
		}
		return LOCTEXT("None", "<None>");
	}
}


void SMVVMFieldEntry::Construct(const FArguments& InArgs)
{
	Field = InArgs._Field;
	OnValidateField = InArgs._OnValidateField;

	ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SAssignNew(Icon, SMVVMFieldIcon)
		]
	+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SAssignNew(Label, STextBlock)
			.TextStyle(InArgs._TextStyle)
		]
		];

	Refresh();
}

void SMVVMFieldEntry::Refresh()
{
	FText ToolTipText = FText::GetEmpty();
	bool bEnabled = true;
	if (OnValidateField.IsBound())
	{
		TValueOrError<bool, FString> Result = OnValidateField.Execute(Field);
		if (Result.HasError())
		{
			ToolTipText = FText::FromString(Result.GetError());
			bEnabled = false;
		}
		else
		{
			bEnabled = true;
		}
	}

	SetEnabled(bEnabled);

	UE::MVVM::FMVVMConstFieldVariant Variant;

	TArray<UE::MVVM::FMVVMConstFieldVariant> Fields = Field.GetFields();
	if (Fields.Num() > 0)
	{
		Variant = Fields.Last();
	}

	if (ToolTipText.IsEmpty())
	{
		ToolTipText = Variant.IsFunction() ? Variant.GetFunction()->GetToolTipText() :
			Variant.IsProperty() ? Variant.GetProperty()->GetToolTipText() :
			FText::GetEmpty();
	}

	SetToolTipText(ToolTipText);

	Icon->RefreshBinding(Variant);
	Label->SetText(Private::GetFieldDisplayName(Variant));
}

void SMVVMFieldEntry::SetField(const FMVVMBlueprintPropertyPath& InField)
{
	Field = InField;

	Refresh();
}

void SMVVMFieldSelector::Construct(const FArguments& InArgs)
{
	OnValidateFieldDelegate = InArgs._OnValidateField;
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	SelectedField = InArgs._SelectedField;
	check(SelectedField.IsSet());

	SelectedSource = InArgs._SelectedSource;
	check(SelectedSource.IsSet());

	AvailableFields = InArgs._AvailableFields;
	check(AvailableFields.IsSet());

	CachedAvailableFields = AvailableFields.Get(TArray<FMVVMBlueprintPropertyPath>());

	BindingMode = InArgs._BindingMode;
	bIsSource = InArgs._IsSource;
	TextStyle = InArgs._TextStyle;

	Refresh();

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoSourceSelected", "No source selected"))
				.TextStyle(FAppStyle::Get(), "HintText")
				.Visibility_Lambda([this]()
				{
					return SelectedSource.Get(UE::MVVM::FBindingSource()).IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]
			+ SOverlay::Slot()
			[
				SAssignNew(FieldComboBox, SComboBox<FMVVMBlueprintPropertyPath>)
				.Visibility_Lambda([this]()
				{
					return SelectedSource.Get(UE::MVVM::FBindingSource()).IsValid() ? EVisibility::Visible : EVisibility::Hidden;
				})
				.OptionsSource(&CachedAvailableFields)
				.InitiallySelectedItem(SelectedField.Get(FMVVMBlueprintPropertyPath()))
				.OnGenerateWidget(this, &SMVVMFieldSelector::OnGenerateFieldWidget)
				.OnSelectionChanged(this, &SMVVMFieldSelector::OnComboBoxSelectionChanged)
				[
					SAssignNew(SelectedEntryWidget, SMVVMFieldEntry)
					.TextStyle(TextStyle)
					.Field(SelectedField.Get(FMVVMBlueprintPropertyPath()))
					.OnValidateField(this, &SMVVMFieldSelector::ValidateField)
				]
			]
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SSimpleButton)
			.Icon(FAppStyle::Get().GetBrush("Icons.X"))
			.ToolTipText(LOCTEXT("ClearField", "Clear field selection."))
			.Visibility(this, &SMVVMFieldSelector::GetClearVisibility)
			.OnClicked(this, &SMVVMFieldSelector::OnClearBinding)
		]
	];
}

void SMVVMFieldSelector::OnComboBoxSelectionChanged(FMVVMBlueprintPropertyPath Selected, ESelectInfo::Type SelectionType)
{
	SelectedEntryWidget->SetField(Selected);

	OnSelectionChangedDelegate.ExecuteIfBound(Selected);
}

void SMVVMFieldSelector::Refresh()
{
	CachedAvailableFields = AvailableFields.Get(TArray<FMVVMBlueprintPropertyPath>());

	if (FieldComboBox.IsValid())
	{
		FieldComboBox->RefreshOptions();
		FieldComboBox->SetSelectedItem(SelectedField.Get(FMVVMBlueprintPropertyPath()));
	}
}

TValueOrError<bool, FString> SMVVMFieldSelector::ValidateField(FMVVMBlueprintPropertyPath Field) const
{
	if (OnValidateFieldDelegate.IsBound())
	{
		return OnValidateFieldDelegate.Execute(Field);
	}

	return MakeValue(true);
}

TSharedRef<SWidget> SMVVMFieldSelector::OnGenerateFieldWidget(FMVVMBlueprintPropertyPath Path) const
{
	return SNew(SMVVMFieldEntry)
		.TextStyle(TextStyle)
		.Field(Path)
		.OnValidateField(this, &SMVVMFieldSelector::ValidateField);
}

EVisibility SMVVMFieldSelector::GetClearVisibility() const
{
	FMVVMBlueprintPropertyPath SelectedPath = SelectedField.Get(FMVVMBlueprintPropertyPath());
	if (!SelectedPath.IsEmpty())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

FReply SMVVMFieldSelector::OnClearBinding()
{
	if (FieldComboBox.IsValid())
	{
		FieldComboBox->ClearSelection();
	}

	if (SelectedEntryWidget.IsValid())
	{
		SelectedEntryWidget->Refresh();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
