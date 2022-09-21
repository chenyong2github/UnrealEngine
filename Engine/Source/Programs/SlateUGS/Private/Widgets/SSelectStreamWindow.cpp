// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSelectStreamWindow.h"

#include "SlateUGSStyle.h"
#include "Framework/Application/SlateApplication.h"

#include "UGSTab.h"
#include "SGameSyncTab.h"
#include "SPrimaryButton.h"
#include "Widgets/Layout/SHeader.h"

#define LOCTEXT_NAMESPACE "UGSNewWorkspaceWindow"

struct FStreamNode
{
	FStreamNode(const FString& InLabel, bool bInIsStream)
		: Label(FText::FromString(InLabel))
		, bIsStream(bInIsStream) {}

	virtual ~FStreamNode() {}

	FText Label;
	bool bIsStream;
	TArray<TSharedPtr<FStreamNode>> Children;
};

void SSelectStreamWindow::Construct(const FArguments& InArgs, UGSTab* InTab)
{
	Tab = InTab;

	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("WindowTitle", "Select Stream"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2D(600, 500))
	[
		SNew(SBox)
		.Padding(30.0f, 15.0f, 30.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SEditableTextBox)
				// Todo: change hint and enable when filters are supported
				.HintText(LOCTEXT("FilterHint", "Filter (under construction, does not work yet)"))
				.IsEnabled(false)
			]
			+SVerticalBox::Slot()
			[
				SNew(STreeView<TSharedPtr<FStreamNode>>)
				.TreeItemsSource(&StreamsTree)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HAlign(HAlign_Right)
				.Padding(10.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SPrimaryButton)
						.Text(LOCTEXT("OkButtonText", "Ok"))
						.OnClicked(this, &SSelectStreamWindow::OnOkClicked)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelButtonText", "Cancel"))
						.OnClicked(this, &SSelectStreamWindow::OnCancelClicked)
					]
				]
			]
		]
	]);
}

FReply SSelectStreamWindow::OnOkClicked()
{
	return FReply::Handled();
}

FReply SSelectStreamWindow::OnCancelClicked()
{
	FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
