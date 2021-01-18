// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlDescription.h"

#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"


#define LOCTEXT_NAMESPACE "SourceControl.Description"

void SSourceControlDescriptionWidget::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow.Get();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16)
			[
				SNew(STextBlock)
				.Text(InArgs._Label)
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(16, 0, 16, 16))
			[
				SAssignNew(TextBox, SMultiLineEditableTextBox)
				.SelectAllTextWhenFocused(true)
				.AutoWrapText(true)
				.Text(InArgs._Text)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text( NSLOCTEXT("SourceControl.Description", "OKButton", "Ok") )
					.OnClicked(this, &SSourceControlDescriptionWidget::OKClicked)
				]
				+SUniformGridPanel::Slot(1,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text( NSLOCTEXT("SourceControl.Description", "CancelButton", "Cancel") )
					.OnClicked(this, &SSourceControlDescriptionWidget::CancelClicked)
				]
			]
		]
	];

	ParentWindow.Pin()->SetWidgetToFocusOnActivate(TextBox);
}

FReply SSourceControlDescriptionWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Pressing escape returns as if the user clicked cancel
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return CancelClicked();
	}

	return FReply::Unhandled();
}

FReply SSourceControlDescriptionWidget::OKClicked()
{
	bResult = true;
	ParentWindow.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}


FReply SSourceControlDescriptionWidget::CancelClicked()
{
	bResult = false;
	ParentWindow.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}

FText SSourceControlDescriptionWidget::GetDescription() const
{
	return TextBox->GetText();
}

bool GetChangelistDescription(
	const TSharedPtr<SWidget>& ParentWidget,
	const FText& InWindowTitle, 
	const FText& InLabel, 
	FText& OutDescription)
{
	FText InitialDescription = OutDescription;
	if (InitialDescription.IsEmpty())
	{
		InitialDescription = LOCTEXT("SourceControl.NewDescription", "<enter description here>");
	}

	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(InWindowTitle)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(600, 400))
		.SupportsMaximize(true)
		.SupportsMinimize(false);

	TSharedRef<SSourceControlDescriptionWidget> SourceControlWidget =
		SNew(SSourceControlDescriptionWidget)
		.ParentWindow(NewWindow)
		.Label(InLabel)
		.Text(InitialDescription);

	NewWindow->SetContent(SourceControlWidget);

	FSlateApplication::Get().AddModalWindow(NewWindow, ParentWidget);

	if (SourceControlWidget->GetResult())
	{
		OutDescription = SourceControlWidget->GetDescription();
	}

	return SourceControlWidget->GetResult();
}

#undef LOCTEXT_NAMESPACE