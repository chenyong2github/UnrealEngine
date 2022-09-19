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
	if (!Field.IsEmpty())
	{
		if (Field.IsProperty())
		{
			return Field.GetProperty()->GetDisplayNameText();
		}
		if (Field.IsFunction())
		{
			return Field.GetFunction()->GetDisplayNameText();
		}
	}
	return LOCTEXT("None", "<None>");
}

FText GetFieldToolTip(const FMVVMConstFieldVariant& Field)
{
	if (!Field.IsEmpty())
	{
		if (Field.IsFunction())
		{
			return Field.GetFunction()->GetToolTipText();
		}
		if (Field.IsProperty())
		{
			return FText::Join(FText::FromString(TEXT("\n")), Field.GetProperty()->GetToolTipText(), FText::FromString(Field.GetProperty()->GetCPPType()));
		}
	}

	return FText::GetEmpty();
}

} // namespace Private

void SFieldEntry::Construct(const FArguments& InArgs)
{
	Field = InArgs._Field;

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
			.Clipping(EWidgetClipping::OnDemand)
		]
	];

	Refresh();
}

void SFieldEntry::Refresh()
{
	UE::MVVM::FMVVMConstFieldVariant Variant;

	TArray<UE::MVVM::FMVVMConstFieldVariant> Fields = Field.GetFields();
	if (Fields.Num() > 0)
	{
		Variant = Fields.Last();
	}

	SetToolTipText(Private::GetFieldToolTip(Variant));

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
