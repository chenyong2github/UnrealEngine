// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleDetailsRowWidget.h"

#include "Library/DMXEntityFixturePatch.h"

#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleDetailsRowWidget"

void SDMXControlConsoleDetailsRowWidget::Construct(const FArguments& InArgs, const FDMXEntityFixturePatchRef InFixturePatchRef)
{
	OnGenerateFromFixturePatch = InArgs._OnGenerateFromFixturePatch;
	OnSelectFixturePatchDetailsRow = InArgs._OnSelectFixturePatchDetailsRow;

	FixturePatchRef = InFixturePatchRef;
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(this, &SDMXControlConsoleDetailsRowWidget::GetBorderImage)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(2.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(FText::FromString(FixturePatchRef.GetFixturePatch()->GetDisplayName()))
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.Padding(5.f)
			[
				SNew(SButton)
				.OnClicked(this, &SDMXControlConsoleDetailsRowWidget::OnGenerateClicked)
				.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleDetailsRowWidget::GetGenerateButtonVisibility))
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(LOCTEXT("GenerateButton", "Generate"))
				]
			]
		]
	];
}

FReply SDMXControlConsoleDetailsRowWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!IsSelected())
		{
			OnSelectFixturePatchDetailsRow.ExecuteIfBound(SharedThis(this));
		}
	}

	return FReply::Unhandled();
}

void SDMXControlConsoleDetailsRowWidget::Select()
{
	bSelected = true;
}

void SDMXControlConsoleDetailsRowWidget::Unselect()
{
	bSelected = false;
}

FReply SDMXControlConsoleDetailsRowWidget::OnGenerateClicked()
{
	OnGenerateFromFixturePatch.ExecuteIfBound(SharedThis(this));

	return FReply::Handled();
}

const FSlateBrush* SDMXControlConsoleDetailsRowWidget::GetBorderImage() const
{
	if (IsHovered())
	{
		if (IsSelected())
		{
			return FAppStyle::GetBrush("DetailsView.CategoryTop");
		}
		else
		{
			return FAppStyle::GetBrush("DetailsView.CategoryTop");
		}
	}
	else
	{
		if (IsSelected())
		{
			return FAppStyle::GetBrush("DetailsView.CategoryTop");
		}
		else
		{
			return FAppStyle::GetBrush("NoBorder");
		}
	}
}

EVisibility SDMXControlConsoleDetailsRowWidget::GetGenerateButtonVisibility() const
{
	return IsSelected() ? EVisibility::Visible : EVisibility::Hidden;
}

#undef LOCTEXT_NAMESPACE
