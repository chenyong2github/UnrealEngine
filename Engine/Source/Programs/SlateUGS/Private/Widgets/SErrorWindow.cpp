// Copyright Epic Games, Inc. All Rights Reserved.

#include "SErrorWindow.h"

#define LOCTEXT_NAMESPACE "ErrorWindow"

void SErrorWindow::Construct(const FArguments& InArgs)
{
	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("WindowTitle", "Error"))
	.SizingRule(ESizingRule::Autosized)
	.MaxWidth(400)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(25.0f, 25.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Justification(ETextJustify::Center)
			.Text(InArgs._ErrorText)
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("ErrorWindowOkayButtonText", "Ok"))
			.OnClicked_Lambda([this]()
			{
				SharedThis(this)->RequestDestroyWindow();
				return FReply::Handled();
			})
		]
	]);
}

#undef LOCTEXT_NAMESPACE
