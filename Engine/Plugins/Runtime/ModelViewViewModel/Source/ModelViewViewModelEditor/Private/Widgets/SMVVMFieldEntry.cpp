// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMFieldEntry.h"
#include "SMVVMFieldIcon.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "MVVMFieldEntry"

namespace UE::MVVM
{

namespace Private
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

} // namespace Private

void SFieldEntry::Construct(const FArguments& InArgs)
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
			SAssignNew(Icon, SFieldIcon)
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

void SFieldEntry::Refresh()
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

void SFieldEntry::SetField(const FMVVMBlueprintPropertyPath& InField)
{
	Field = InField;

	Refresh();
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
