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

	FMVVMConstFieldVariant GetSelectedFieldFromHelpers(const TArray<IFieldPathHelper*>& PathHelpers)
	{
		bool bFirst = true;
		FMVVMConstFieldVariant Selected;

		for (const IFieldPathHelper* Helper : PathHelpers)
		{
			const FMVVMConstFieldVariant ThisSelection = Helper->GetSelectedField();
			if (bFirst)
			{
				Selected = ThisSelection;
				bFirst = false;
			}
			else if (Selected != ThisSelection)
			{
				Selected.Reset();
				break;
			}
		}

		return Selected;
	}
}

TOptional<FBindingSource> SMVVMFieldSelector::GetSelectedSource() const
{
	TOptional<FBindingSource> Source;
	for (const IFieldPathHelper* Helper : PathHelpers)
	{
		if (!Source.IsSet())
		{
			Source = Helper->GetSelectedSource();
		}
		else if (Source != Helper->GetSelectedSource())
		{
			Source.Reset();
			break;
		}
	}

	return Source;
}

void SMVVMFieldSelector::Construct(const FArguments& InArgs)
{
	PathHelpers = InArgs._PathHelpers;
	CounterpartHelpers = InArgs._CounterpartHelpers;
	OnValidateFieldDelegate = InArgs._OnValidateField;
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	BindingMode = InArgs._BindingMode;
	bIsSource = InArgs._IsSource;
	TextStyle = InArgs._TextStyle;

	FMVVMConstFieldVariant SelectedField = Private::GetSelectedFieldFromHelpers(PathHelpers);
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
					return SelectedSource.IsSet() ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]
			+ SOverlay::Slot()
			[
				SAssignNew(FieldComboBox, SComboBox<FMVVMConstFieldVariant>)
				.Visibility_Lambda([this]()
				{
					return SelectedSource.IsSet() ? EVisibility::Visible : EVisibility::Hidden;
				})
				.OptionsSource(&AvailableFields)
				.InitiallySelectedItem(SelectedField)
				.OnGenerateWidget(this, &SMVVMFieldSelector::OnGenerateFieldWidget)
				.OnSelectionChanged(this, &SMVVMFieldSelector::OnComboBoxSelectionChanged)
				[
					SAssignNew(SelectedEntry, SMVVMFieldEntry)
					.TextStyle(TextStyle)
					.Field(SelectedField)
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

void SMVVMFieldSelector::OnComboBoxSelectionChanged(FMVVMConstFieldVariant Selected, ESelectInfo::Type SelectionType)
{
	SelectedEntry->SetField(Selected);

	OnSelectionChangedDelegate.ExecuteIfBound(Selected);
}

void SMVVMFieldSelector::Refresh()
{
	AvailableFields.Reset();

	SelectedSource = GetSelectedSource();

	TSet<FMVVMConstFieldVariant> CompatibleFields;
	TSet<FMVVMConstFieldVariant> IncompatibleFields;

	FMVVMConstFieldVariant SelectedField = Private::GetSelectedFieldFromHelpers(PathHelpers);
	FMVVMConstFieldVariant CounterpartSelectedField = Private::GetSelectedFieldFromHelpers(CounterpartHelpers);

	bool bFirst = true;

	for (IFieldPathHelper* Helper : PathHelpers)
	{
		TSet<FMVVMConstFieldVariant> AllFields;
		Helper->GetAvailableFields(AllFields);

		if (bFirst)
		{
			bFirst = false;

			for (const FMVVMConstFieldVariant& Field : AllFields)
			{
				TValueOrError<bool, FString> Result = IsValidBindingForField(Field, CounterpartSelectedField);
				if (Result.HasValue() && Result.GetValue() == true)
				{
					CompatibleFields.Add(Field);
				}
				else
				{
					IncompatibleFields.Add(Field);
				}
			}
		}
		else
		{
			CompatibleFields.Intersect(AllFields);
			IncompatibleFields.Intersect(AllFields);
		}
	}

	AvailableFields.Append(CompatibleFields.Array());

	// put all incompatible properties at the end so they don't just disappear from the list without explanation
	AvailableFields.Append(IncompatibleFields.Array());
	
	if (FieldComboBox.IsValid())
	{
		FieldComboBox->RefreshOptions();
		FieldComboBox->SetSelectedItem(SelectedField);
	}
}

TValueOrError<bool, FString> SMVVMFieldSelector::IsValidBindingForField(const FMVVMConstFieldVariant& Field, const FMVVMConstFieldVariant& CounterpartField) const
{
	// if we have a delegate bound, use that instead
	if (OnValidateFieldDelegate.IsBound())
	{
		return OnValidateFieldDelegate.Execute(Field);
	}

	if (CounterpartField.IsEmpty() || Field.IsEmpty())
	{
		return MakeValue(true);
	}

	if (!BindingMode.IsSet())
	{
		return MakeValue(true);
	}

	EMVVMBindingMode Mode = BindingMode.Get();

	bool bToDestination = UE::MVVM::IsForwardBinding(Mode);
	if (!bIsSource)
	{
		bToDestination = !bToDestination;
	}

	UMVVMSubsystem::FConstDirectionalBindingArgs Args;
	Args.SourceBinding = bToDestination ? Field : CounterpartField;
	Args.DestinationBinding = bToDestination ? CounterpartField : Field;

	UMVVMSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMVVMSubsystem>();

	TValueOrError<bool, FString> Result = Subsystem->IsBindingValid(Args);

	// check in the other direction as well if this is a two-way binding
	if (Mode == EMVVMBindingMode::TwoWay &&
		Result.HasValue() && Result.GetValue() == true)
	{
		Args.SourceBinding = bToDestination ? CounterpartField : Field;
		Args.DestinationBinding = bToDestination ? Field : CounterpartField;

		return Subsystem->IsBindingValid(Args);
	}

	return Result;
}

TValueOrError<bool, FString> SMVVMFieldSelector::ValidateField(FMVVMConstFieldVariant Field) const
{
	return IsValidBindingForField(Field, Private::GetSelectedFieldFromHelpers(CounterpartHelpers));
}

TSharedRef<SWidget> SMVVMFieldSelector::OnGenerateFieldWidget(FMVVMConstFieldVariant Field) const
{
	return SNew(SMVVMFieldEntry)
		.TextStyle(TextStyle)
		.Field(Field)
		.OnValidateField(this, &SMVVMFieldSelector::ValidateField);
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

	if (ToolTipText.IsEmpty())
	{
		ToolTipText = Field.IsFunction() ? Field.GetFunction()->GetToolTipText() :
			Field.IsProperty() ? Field.GetProperty()->GetToolTipText() :
			FText::GetEmpty();
	}

	SetToolTipText(ToolTipText);

	Icon->RefreshBinding(Field);
	Label->SetText(Private::GetFieldDisplayName(Field));
}

void SMVVMFieldEntry::SetField(const UE::MVVM::FMVVMConstFieldVariant& InField)
{
	Field = InField;

	Refresh();
}

EVisibility SMVVMFieldSelector::GetClearVisibility() const
{
	for (const IFieldPathHelper* Helper : PathHelpers)
	{
		const FMVVMConstFieldVariant ThisSelection = Helper->GetSelectedField();
		if (!ThisSelection.IsEmpty())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FReply SMVVMFieldSelector::OnClearBinding()
{
	if (FieldComboBox.IsValid())
	{
		FieldComboBox->ClearSelection();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
