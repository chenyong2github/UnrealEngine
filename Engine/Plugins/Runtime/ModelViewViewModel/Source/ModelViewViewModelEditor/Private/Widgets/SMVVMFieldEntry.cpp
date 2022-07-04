
#include "SMVVMFieldEntry.h"
#include "SMVVMFieldIcon.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "MVVMFieldEntry"

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

#undef LOCTEXT_NAMESPACE
