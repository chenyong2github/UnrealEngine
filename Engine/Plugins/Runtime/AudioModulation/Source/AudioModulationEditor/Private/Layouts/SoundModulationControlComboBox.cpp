// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationControlComboBox.h"

#include "AudioModulationSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"


#define LOCTEXT_NAMESPACE "SoundModulationParameter"
void SSoundModulationControlComboBox::Construct(const FArguments& InArgs)
{
	ControlNameProperty = InArgs._ControlNameProperty;

	ControlNameStrings.Add(MakeShared<FString>(TEXT("None")));

	TSharedPtr<FString> SetControlValue;
	if (const UAudioModulationSettings* ModSettings = GetDefault<UAudioModulationSettings>())
	{
		for (FName Name : ModSettings->ControlNames)
		{
			TSharedPtr<FString> NewName = MakeShared<FString>(Name.ToString());
			if (Name == InArgs._InitControlName)
			{
				SetControlValue = NewName;
			}
			ControlNameStrings.Add(NewName);
		}
	}
	ControlNameStrings.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B) { return A->Compare(*B.Get()) < 0; });

	SSearchableComboBox::Construct(SSearchableComboBox::FArguments()
		.OptionsSource(&ControlNameStrings)
		.OnGenerateWidget(this, &SSoundModulationControlComboBox::MakeControlComboWidget)
		.OnSelectionChanged(this, &SSoundModulationControlComboBox::OnControlChanged)
		.OnComboBoxOpening(this, &SSoundModulationControlComboBox::OnControlComboOpening)
		.ContentPadding(0)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &SSoundModulationControlComboBox::GetControlComboBoxContent)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ToolTipText(ControlNameProperty->GetToolTipText())
	);
}

FText SSoundModulationControlComboBox::GetControlComboBoxContent() const
{
	FName ControlSourceName;
	if (ControlNameProperty->GetValue(ControlSourceName) == FPropertyAccess::Result::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::FromString(*GetControlString(ControlSourceName).Get());
}

TSharedPtr<FString> SSoundModulationControlComboBox::GetControlString(FName ControlName) const
{
	FString ControlString = ControlName.ToString();

	// go through profile and see if it has mine
	for (int32 Index = 1; Index < ControlNameStrings.Num(); ++Index)
	{
		if (ControlString == *ControlNameStrings[Index])
		{
			return ControlNameStrings[Index];
		}
	}

	return ControlNameStrings[0];
}

TSharedRef<SWidget> SSoundModulationControlComboBox::MakeControlComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem)).Font(IDetailLayoutBuilder::GetDetailFont());
}

void SSoundModulationControlComboBox::OnControlChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	// if set from code, ignore validation
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		if (NewValue == TEXT("None"))
		{
			NewValue = TEXT("");
		}

		ensure(ControlNameProperty->SetValue(NewValue) == FPropertyAccess::Result::Success);
	}
}

void SSoundModulationControlComboBox::OnControlComboOpening()
{
	FName ControlName;
	if (ControlNameProperty->GetValue(ControlName) != FPropertyAccess::Result::MultipleValues)
	{
		TSharedPtr<FString> ComboStringPtr = GetControlString(ControlName);
		if (ComboStringPtr.IsValid())
		{
			SetSelectedItem(ComboStringPtr);
		}
	}
}
#undef LOCTEXT_NAMESPACE