// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMutableColorPreviewBox.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/SlateColor.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

void SMutableColorPreviewBox::Construct(const FArguments& InArgs)
{
	this->Color = InArgs._BoxColor.Get();

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(this,&SMutableColorPreviewBox::GetColor)
		]
	];
}

void SMutableColorPreviewBox::SetColor(const FSlateColor& InColor)
{
	this->Color = InColor;
}

FSlateColor SMutableColorPreviewBox::GetColor() const
{
	return this->Color;
}
#undef LOCTEXT_NAMESPACES