// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFixturePatchRowWidget.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleFixturePatchRowWidget"

void SDMXControlConsoleFixturePatchRowWidget::Construct(const FArguments& InArgs, const FDMXEntityFixturePatchRef InFixturePatchRef)
{
	OnGenerateOnLastRow = InArgs._OnGenerateOnLastRow;
	OnGenerateOnNewRow = InArgs._OnGenerateOnNewRow;
	OnGenerateOnSelectedFaderGroup = InArgs._OnGenerateOnSelectedFaderGroup;
	OnSelectFixturePatchRow = InArgs._OnSelectFixturePatchRow;

	FixturePatchRef = InFixturePatchRef;
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.MaxHeight(32.f)
		[
			SNew(SBorder)
			.BorderImage(this, &SDMXControlConsoleFixturePatchRowWidget::GetBorderImage)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(0.4f)
				.Padding(20.f, 2.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(FText::FromString(FixturePatchRef.GetFixturePatch()->GetDisplayName()))
				]

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.FillWidth(0.6f)
				.Padding(2.f)
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHorizontalBox)
						
						// 'Add (Next)' button
						+SHorizontalBox::Slot()
						.FillWidth(0.5f)
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
							.OnClicked(this, &SDMXControlConsoleFixturePatchRowWidget::OnAddNextClicked)
							.ToolTipText(LOCTEXT("AddNextTooltip", "Add a Fader Group generated from this Fixture Patch next to selection."))
							.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleFixturePatchRowWidget::GetAddNextButtonVisibility))
							[
								SNew(STextBlock)
								.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
								.Text(LOCTEXT("AddNextButton", "+ Add"))
							]
						]

						// 'Add (Row)' button
						+ SHorizontalBox::Slot()
						.FillWidth(0.5f)
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
							.OnClicked(this, &SDMXControlConsoleFixturePatchRowWidget::OnAddRowClicked)
							.ToolTipText(LOCTEXT("AddRowTooltip", "Add a Fader Group generated from this Fixture Patch on a new row."))
							.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleFixturePatchRowWidget::GetAddRowButtonVisibility))
							[
								SNew(STextBlock)
								.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
								.Text(LOCTEXT("AddRowButton", "+ Row"))
							]
						]
					]

					// 'Generate' button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 4.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
						.OnClicked(this, &SDMXControlConsoleFixturePatchRowWidget::OnGenerateClicked)
						.ToolTipText(LOCTEXT("AddNextTooltip", "Edit selection generating a Fader Group from this Fixture Patch."))
						.Visibility(TAttribute<EVisibility>(this, &SDMXControlConsoleFixturePatchRowWidget::GetGenerateButtonVisibility))
						[
							SNew(STextBlock)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							.Text(LOCTEXT("GenerateButton", "Generate"))
						]
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		]
	];
}

FReply SDMXControlConsoleFixturePatchRowWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!IsSelected())
		{
			OnSelectFixturePatchRow.ExecuteIfBound(SharedThis(this));
		}
	}

	return FReply::Unhandled();
}

void SDMXControlConsoleFixturePatchRowWidget::Select()
{
	bSelected = true;
}

void SDMXControlConsoleFixturePatchRowWidget::Unselect()
{
	bSelected = false;
}

FReply SDMXControlConsoleFixturePatchRowWidget::OnAddNextClicked()
{
	OnGenerateOnLastRow.ExecuteIfBound(SharedThis(this));

	return FReply::Handled();
}

FReply SDMXControlConsoleFixturePatchRowWidget::OnAddRowClicked()
{
	OnGenerateOnNewRow.ExecuteIfBound(SharedThis(this));

	return FReply::Handled();
}

FReply SDMXControlConsoleFixturePatchRowWidget::OnGenerateClicked()
{
	OnGenerateOnSelectedFaderGroup.ExecuteIfBound(SharedThis(this));

	return FReply::Handled();
}

const FSlateBrush* SDMXControlConsoleFixturePatchRowWidget::GetBorderImage() const
{
	if (IsHovered())
	{
		return FAppStyle::GetBrush("DetailsView.CategoryTop");
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

EVisibility SDMXControlConsoleFixturePatchRowWidget::GetAddNextButtonVisibility() const
{
	const UDMXControlConsole* ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole || ControlConsole->GetFaderGroupRows().IsEmpty())
	{
		return EVisibility::Collapsed;
	}

	const bool bIsVisible = IsSelected() || IsHovered();
	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SDMXControlConsoleFixturePatchRowWidget::GetAddRowButtonVisibility() const
{
	const bool bIsVisible = IsSelected() || IsHovered();
	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SDMXControlConsoleFixturePatchRowWidget::GetGenerateButtonVisibility() const
{
	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	if (SelectionHandler->GetSelectedFaderGroups().IsEmpty())
	{
		return EVisibility::Collapsed;
	}

	const bool bIsVisible = IsSelected() || IsHovered();
	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

#undef LOCTEXT_NAMESPACE
