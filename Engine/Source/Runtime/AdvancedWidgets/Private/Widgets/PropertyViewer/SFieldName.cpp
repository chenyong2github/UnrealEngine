// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/PropertyViewer/SFieldName.h"

#include "Application/SlateApplicationBase.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "Widgets/PropertyViewer/SFieldIcon.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


namespace UE::PropertyViewer
{

void SFieldName::Construct(const FArguments& InArgs, const UClass* Class)
{
	check(Class);

	TSharedPtr<SWidget> Icon;
	if (InArgs._bShowIcon)
	{
		Icon = SNew(SFieldIcon, Class);
	}

	FText DisplayName;
	if (InArgs._OverrideDisplayName.IsSet())
	{
		DisplayName = InArgs._OverrideDisplayName.GetValue();
	}
	else
	{
#if WITH_EDITORONLY_DATA
		DisplayName = InArgs._bSanitizeName ? Class->GetDisplayNameText() : FText::FromName(Class->GetFName());
#else
		DisplayName = FText::FromName(Class->GetFName());
#endif
	}

	Field = Class;
	Construct(InArgs, DisplayName, Icon);
}


void SFieldName::Construct(const FArguments& InArgs, const UScriptStruct* Struct)
{
	check(Struct);

	TSharedPtr<SWidget> Icon;
	if (InArgs._bShowIcon)
	{
		Icon = SNew(SFieldIcon, Struct);
	}

	FText DisplayName;
	if (InArgs._OverrideDisplayName.IsSet())
	{
		DisplayName = InArgs._OverrideDisplayName.GetValue();
	}
	else
	{
#if WITH_EDITORONLY_DATA
		DisplayName = InArgs._bSanitizeName ? Struct->GetDisplayNameText() : FText::FromName(Struct->GetFName());
#else
		DisplayName = FText::FromName(Class->GetFName());
#endif
	}

	Field = Struct;
	Construct(InArgs, DisplayName, Icon);
}


void SFieldName::Construct(const FArguments& InArgs, const FProperty* Property)
{
	check(Property);

	TSharedPtr<SWidget> Icon;
	if (InArgs._bShowIcon)
	{
		Icon = SNew(SFieldIcon, Property);
	}
	
	FText DisplayName;
	if (InArgs._OverrideDisplayName.IsSet())
	{
		DisplayName = InArgs._OverrideDisplayName.GetValue();
	}
	else
	{
#if WITH_EDITORONLY_DATA
		DisplayName = InArgs._bSanitizeName ? Property->GetDisplayNameText() : FText::FromName(Property->GetFName());
#else
		DisplayName = FText::FromName(Class->GetFName());
#endif
	}

	Field = Property;
	Construct(InArgs, DisplayName, Icon);
}


void SFieldName::Construct(const FArguments& InArgs, const UFunction* Function)
{
	check(Function);

	TSharedPtr<SWidget> Icon;
	if (InArgs._bShowIcon)
	{
		Icon = SNew(SFieldIcon, Function);
	}

	FText DisplayName;
	if (InArgs._OverrideDisplayName.IsSet())
	{
		DisplayName = InArgs._OverrideDisplayName.GetValue();
	}
	else
	{
#if WITH_EDITORONLY_DATA
		DisplayName = InArgs._bSanitizeName ? Function->GetDisplayNameText() : FText::FromName(Function->GetFName());
#else
		DisplayName = FText::FromName(Class->GetFName());
#endif
	}

	Field = Function;
	Construct(InArgs, DisplayName, Icon);
}


void SFieldName::Construct(const FArguments& InArgs, const FText& DisplayName, TSharedPtr<SWidget> Icon)
{
	if (Icon)
	{
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				Icon.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				SAssignNew(NameBlock, STextBlock)
				.Text(DisplayName)
				.HighlightText(InArgs._HighlightText)
			]
		];
	}
	else
	{
		ChildSlot
		[
			SAssignNew(NameBlock, STextBlock)
			.Text(DisplayName)
			.HighlightText(InArgs._HighlightText)
		];
	}

#if WITH_EDITORONLY_DATA
	SetToolTip(TAttribute<TSharedPtr<IToolTip>>::CreateSP(this, &SFieldName::CreateToolTip));
#endif
}


void SFieldName::SetHighlightText(TAttribute<FText> InHighlightText)
{
	if (NameBlock)
	{
		NameBlock->SetHighlightText(MoveTemp(InHighlightText));
	}
}

TSharedPtr<IToolTip> SFieldName::CreateToolTip() const
{
#if WITH_EDITORONLY_DATA
	if (FProperty* PropertyPtr = Field.Get<FProperty>())
	{
		return FSlateApplicationBase::Get().MakeToolTip(PropertyPtr->GetToolTipText());
	}
	if (UField* FieldPtr = Field.Get<UField>())
	{
		return FSlateApplicationBase::Get().MakeToolTip(FieldPtr->GetToolTipText());
	}
#endif
	return TSharedPtr<IToolTip>();
}

} //namespace
