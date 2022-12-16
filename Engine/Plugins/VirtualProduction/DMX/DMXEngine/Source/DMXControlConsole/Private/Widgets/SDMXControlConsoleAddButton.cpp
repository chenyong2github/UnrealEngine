// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleAddButton.h"

#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/Images/SImage.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleAddButton"

namespace DMXControlConsoleAddButton
{
	static FSlateColor HoveringColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f));
};

void SDMXControlConsoleAddButton::Construct(const FArguments& InArgs)
{
	OnClicked = InArgs._OnClicked;

	ChildSlot
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ClickMethod(EButtonClickMethod::MouseDown)
			.ContentPadding(0.f)
			.ForegroundColor(FSlateColor::UseForeground())
			.IsFocusable(false)
			.OnClicked(OnClicked)
			.Visibility(InArgs._Visibility)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
				.ColorAndOpacity(this, &SDMXControlConsoleAddButton::GetButtonColor)
			]
		];
}

FSlateColor SDMXControlConsoleAddButton::GetButtonColor() const
{
	if (IsHovered())
	{
		return DMXControlConsoleAddButton::HoveringColor;
	}
	else
	{
		return FSlateColor::UseSubduedForeground();
	}
}

#undef LOCTEXT_NAMESPACE
