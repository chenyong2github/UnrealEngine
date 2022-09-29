// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingAttributeNamesComboBox.h"

#include "Algo/Find.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingAttributeNamesComboBox"

void SDMXPixelMappingAttributeNamesComboBox::Construct(const FArguments& InArgs, const TArray<FName>& InOptions)
{
	for (const FName& AttributeName : InOptions)
	{
		Options.Add(MakeShared<FName>(AttributeName));
	}

	OnSelectionChanged = InArgs._OnSelectionChanged;

	// Always have at least one option to display
	if (Options.IsEmpty())
	{
		Options.Add(MakeShared<FName>("No attributes available"));
	}

	ChildSlot
		[
			SAssignNew(ComboBox, SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&Options)
			.OnSelectionChanged(this, &SDMXPixelMappingAttributeNamesComboBox::HandleSelectionChanged)
			.OnGenerateWidget(this, &SDMXPixelMappingAttributeNamesComboBox::OnGenerateComboBoxEntry)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
					{
						if (bHasMultipleValues)
						{
							return LOCTEXT("MultipleValuesText", "Multiple Values");
						}
						else
						{
							check(!Options.IsEmpty());

							const TSharedPtr<FName> SelectedItem = ComboBox->GetSelectedItem();
							return SelectedItem.IsValid() ? FText::FromString(SelectedItem->ToString()) : FText::FromString(TEXT("Invalid selection"));
						}
					})
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		];
}

void SDMXPixelMappingAttributeNamesComboBox::SetSelection(const FName& Selection)
{
	bHasMultipleValues = false;
	const TSharedPtr<FName>* SelectionPtr = Algo::FindByPredicate(Options, [&Selection](const TSharedPtr<FName>& AttributeName)
		{
			return *AttributeName == Selection;
		});

	ensureMsgf(SelectionPtr, TEXT("Cannot set selection of SDMXPixelMappingAttributeNamesComboBox. Selection %s is not provided as an option."), *Selection.ToString());
	if (SelectionPtr)
	{
		ComboBox->SetSelectedItem(*SelectionPtr);
	}
}

void SDMXPixelMappingAttributeNamesComboBox::SetHasMultipleValues()
{
	bHasMultipleValues = true;
}

TSharedRef<SWidget> SDMXPixelMappingAttributeNamesComboBox::OnGenerateComboBoxEntry(TSharedPtr<FName> AttributeName) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(AttributeName->ToString()))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SDMXPixelMappingAttributeNamesComboBox::HandleSelectionChanged(TSharedPtr<FName> NewValue, ESelectInfo::Type)
{
	bHasMultipleValues = false;

	OnSelectionChanged.ExecuteIfBound(*NewValue.Get());
}

#undef LOCTEXT_NAMESPACE
