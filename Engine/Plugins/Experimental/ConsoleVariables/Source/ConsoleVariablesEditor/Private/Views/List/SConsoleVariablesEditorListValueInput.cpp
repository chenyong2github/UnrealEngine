// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorListValueInput.h"

#include "ConsoleVariablesEditorListRow.h"
#include "ConsoleVariablesEditorModule.h"
#include "ConsoleVariablesEditorProjectSettings.h"

#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

SConsoleVariablesEditorListValueInput::~SConsoleVariablesEditorListValueInput()
{
	Item.Reset();
}

TSharedRef<SConsoleVariablesEditorListValueInput> SConsoleVariablesEditorListValueInput::GetInputWidget(
	const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	const FConsoleVariablesEditorListRowPtr PinnedItem = InRow.Pin();

	check(PinnedItem);
	
	if (PinnedItem->GetCommandInfo().IsValid())
	{
		const TSharedPtr<FConsoleVariablesEditorCommandInfo> PinnedInfo = PinnedItem->GetCommandInfo().Pin();

		if (TObjectPtr<IConsoleVariable> Variable = PinnedInfo->ConsoleVariablePtr)
		{
			if (Variable->IsVariableFloat())
			{
				return SNew(SConsoleVariablesEditorListValueInput_Float, InRow);
			}

			if (Variable->IsVariableInt())
			{
				return SNew(SConsoleVariablesEditorListValueInput_Int, InRow);
			}

			if (Variable->IsVariableString())
			{
				return SNew(SConsoleVariablesEditorListValueInput_String, InRow);
			}
		}
	}

	// bool
	return SNew(SConsoleVariablesEditorListValueInput_Bool, InRow);
}

const FString& SConsoleVariablesEditorListValueInput::GetCachedValue() const
{
	return CachedValue;
}

void SConsoleVariablesEditorListValueInput::SetCachedValue(const FString& NewCachedValue)
{
	CachedValue = NewCachedValue;
}

bool SConsoleVariablesEditorListValueInput::IsRowChecked() const
{
	return Item.Pin()->IsRowChecked();
}

void SConsoleVariablesEditorListValueInput_Float::Construct(const FArguments& InArgs,
                                                            const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	Item = InRow;

	ProjectSettingsPtr = GetMutableDefault<UConsoleVariablesEditorProjectSettings>();
	
	ChildSlot
	[
		SAssignNew(InputWidget, SSpinBox<float>)
		.MaxFractionalDigits(3)
		.Value_Lambda([this]
		{
			if (Item.IsValid() &&
				(Item.Pin()->GetWidgetCheckedState() == ECheckBoxState::Checked ||
					(ProjectSettingsPtr && ProjectSettingsPtr->UncheckedRowDisplayType == EConsoleVariablesEditorRowDisplayType::ShowCurrentValue)))
			{
				return FCString::Atof(*Item.Pin()->GetCommandInfo().Pin()->ConsoleVariablePtr->GetString());
			}

			return FCString::Atof(*GetCachedValue());
		})
		.OnValueChanged_Lambda([this] (const float InValue)
		{
			if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())
			{
				const FString ValueAsString = FString::SanitizeFloat(InValue);
				
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueAsString);

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(PinnedItem->GetCommandInfo().Pin()->Command, ValueAsString);
			}
			
			SetCachedValue(FString::SanitizeFloat(InValue));
		})
		.IsEnabled(this, &SConsoleVariablesEditorListValueInput::IsRowChecked)
	];

	SetCachedValue(GetInputValueAsString());
}

void SConsoleVariablesEditorListValueInput_Float::SetInputValue(const FString& InValueAsString)
{
	InputWidget->SetValue(FCString::Atof(*InValueAsString));
}

FString SConsoleVariablesEditorListValueInput_Float::GetInputValueAsString()
{
	return FString::SanitizeFloat(GetInputValue());
}

float SConsoleVariablesEditorListValueInput_Float::GetInputValue() const
{
	return InputWidget->GetValue();
}

void SConsoleVariablesEditorListValueInput_Int::Construct(const FArguments& InArgs,
                                                          const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	Item = InRow;

	ProjectSettingsPtr = GetMutableDefault<UConsoleVariablesEditorProjectSettings>();
	
	ChildSlot
	[
		SAssignNew(InputWidget, SSpinBox<int32>)
		.Value_Lambda([this]
		{
			if (Item.IsValid() &&
				(Item.Pin()->GetWidgetCheckedState() == ECheckBoxState::Checked ||
					(ProjectSettingsPtr && ProjectSettingsPtr->UncheckedRowDisplayType == EConsoleVariablesEditorRowDisplayType::ShowCurrentValue)))
			{
				return FCString::Atoi(*Item.Pin()->GetCommandInfo().Pin()->ConsoleVariablePtr->GetString());
			}

			return FCString::Atoi(*GetCachedValue());
		})
		.OnValueChanged_Lambda([this] (const int32 InValue)
		{
			if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())
			{
				const FString ValueAsString = FString::FromInt(InValue);
				
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueAsString);

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(PinnedItem->GetCommandInfo().Pin()->Command, ValueAsString);
			}

			SetCachedValue(FString::FromInt(InValue));
		})
		.IsEnabled(this, &SConsoleVariablesEditorListValueInput::IsRowChecked)
	];
	
	SetCachedValue(GetInputValueAsString());
}

void SConsoleVariablesEditorListValueInput_Int::SetInputValue(const FString& InValueAsString)
{
	InputWidget->SetValue(FCString::Atoi(*InValueAsString));
}

FString SConsoleVariablesEditorListValueInput_Int::GetInputValueAsString()
{
	return FString::FromInt(GetInputValue());
}

int32 SConsoleVariablesEditorListValueInput_Int::GetInputValue() const
{
	return InputWidget->GetValue();
}

void SConsoleVariablesEditorListValueInput_String::Construct(const FArguments& InArgs,
                                                             const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	Item = InRow;

	ProjectSettingsPtr = GetMutableDefault<UConsoleVariablesEditorProjectSettings>();
	
	ChildSlot
	[
		SAssignNew(InputWidget, SEditableText)
		.Text_Lambda([this]
		{
			if (Item.IsValid() &&
				(Item.Pin()->GetWidgetCheckedState() == ECheckBoxState::Checked ||
					(ProjectSettingsPtr && ProjectSettingsPtr->UncheckedRowDisplayType == EConsoleVariablesEditorRowDisplayType::ShowCurrentValue)))
			{
				return FText::FromString(*Item.Pin()->GetCommandInfo().Pin()->ConsoleVariablePtr->GetString());
			}

			return FText::FromString(GetCachedValue());
		})
		.OnTextCommitted_Lambda([this] (const FText& InValue, ETextCommit::Type InTextCommitType)
		{
			if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())
			{
				const FString ValueAsString = InValue.ToString();
				
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueAsString);

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(PinnedItem->GetCommandInfo().Pin()->Command, ValueAsString);
			}

			SetCachedValue(InValue.ToString());
		})
		.IsEnabled(this, &SConsoleVariablesEditorListValueInput::IsRowChecked)
	];

	SetCachedValue(GetInputValueAsString());
}

void SConsoleVariablesEditorListValueInput_String::SetInputValue(const FString& InValueAsString)
{
	InputWidget->SetText(FText::FromString(InValueAsString));
}

FString SConsoleVariablesEditorListValueInput_String::GetInputValueAsString()
{
	return GetInputValue();
}

FString SConsoleVariablesEditorListValueInput_String::GetInputValue() const
{
	return InputWidget->GetText().ToString();
}

void SConsoleVariablesEditorListValueInput_Bool::Construct(const FArguments& InArgs,
                                                           const TWeakPtr<FConsoleVariablesEditorListRow> InRow)
{
	Item = InRow;

	ProjectSettingsPtr = GetMutableDefault<UConsoleVariablesEditorProjectSettings>();
	
	ChildSlot
	[
		SAssignNew(InputWidget, SSpinBox<int32>)
		.Value_Lambda([this]
		{
			if (Item.IsValid() &&
				(Item.Pin()->GetWidgetCheckedState() == ECheckBoxState::Checked ||
					(ProjectSettingsPtr && ProjectSettingsPtr->UncheckedRowDisplayType == EConsoleVariablesEditorRowDisplayType::ShowCurrentValue)))
			{
				return FCString::Atoi(*Item.Pin()->GetCommandInfo().Pin()->ConsoleVariablePtr->GetString());
			}

			return FCString::Atoi(*GetCachedValue());
		})
		.MinSliderValue(0)
		.MaxSliderValue(2)
		.OnValueChanged_Lambda([this] (const int32 InValue)
		{
			if (const TSharedPtr<FConsoleVariablesEditorListRow> PinnedItem = Item.Pin())
			{
				const FString ValueAsString = FString::FromInt(InValue);
				
				PinnedItem->GetCommandInfo().Pin()->ExecuteCommand(ValueAsString);

				FConsoleVariablesEditorModule::Get().SendMultiUserConsoleVariableChange(PinnedItem->GetCommandInfo().Pin()->Command, ValueAsString);
			}

			SetCachedValue(FString::FromInt(InValue));
		})
		.IsEnabled(this, &SConsoleVariablesEditorListValueInput::IsRowChecked)
	];
	SetCachedValue(GetInputValueAsString());
}

void SConsoleVariablesEditorListValueInput_Bool::SetInputValue(const FString& InValueAsString)
{
	if (InValueAsString.IsNumeric())
	{
		InputWidget->SetValue(FMath::Clamp(FCString::Atoi(*InValueAsString), 0, 2));
	}
	else
	{
		InputWidget->SetValue(2);
		
		if (InValueAsString.TrimStartAndEnd().ToLower() == "true")
		{
			InputWidget->SetValue(1);
		}
		else if (InValueAsString.TrimStartAndEnd().ToLower() == "false")
		{
			InputWidget->SetValue(0);
		}
	}
}

FString SConsoleVariablesEditorListValueInput_Bool::GetInputValueAsString()
{
	return FString::FromInt(GetInputValue());
}

int32 SConsoleVariablesEditorListValueInput_Bool::GetInputValue() const
{
	return InputWidget->GetValue();
}

bool SConsoleVariablesEditorListValueInput_Bool::GetInputValueAsBool() const
{
	return GetInputValue() == 1 ? true : false;
}

FString SConsoleVariablesEditorListValueInput_Bool::GetBoolValueAsString() const
{
	return GetInputValueAsBool() ? "true" : "false";
}

#undef LOCTEXT_NAMESPACE
