// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPopupTextWindow.h"

#define LOCTEXT_NAMESPACE "PopupTextWindow"

void SPopupTextWindow::Construct(const FArguments& InArgs)
{
	SWindow::Construct(SWindow::FArguments()
	.Title(InArgs._TitleText)
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
			.Justification(InArgs._BodyTextJustification)
			.Text(InArgs._BodyText)
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("PopupTextWindowOkayButtonText", "Ok"))
			.OnClicked_Lambda([this]()
			{
				SharedThis(this)->RequestDestroyWindow();
				return FReply::Handled();
			})
		]
	]);
}

#undef LOCTEXT_NAMESPACE
