// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEmptyTab.h"

#include "SWorkspaceWindow.h"
#include "Styling/AppStyle.h"

#include "UGSTab.h"

#define LOCTEXT_NAMESPACE "UGSEmptyTab"

void SEmptyTab::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;

	this->ChildSlot
	[
		SNew(SBox)
		.WidthOverride(800)
		.HeightOverride(600)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.AutoWidth()
			.Padding(10.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("AboutScreen.UnrealLogo")) // Todo: figure out how to get logo to show up
			]
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			.Padding(10.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.VAlign(VAlign_Bottom)
				.AutoHeight()
				.Padding(0.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GetStartedText", "To get started, open an Unreal project file on your hard drive."))
				]
				+SVerticalBox::Slot()
				.VAlign(VAlign_Top)
				.AutoHeight()
				.Padding(0.0f, 5.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Text(LOCTEXT("OpenProject", "OpenProject"))
						.OnClicked(this, &SEmptyTab::OnOpenProjectClicked)
					]
				]
			]
		]
	];
}

FReply SEmptyTab::OnOpenProjectClicked()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
	.Title(LOCTEXT("WindowTitle", "Open Project"))
	.SizingRule(ESizingRule::Autosized)
	[
		SNew(SWorkspaceWindow)
	];

	FSlateApplication& SlateApplication = FSlateApplication::Get();
	SlateApplication.AddModalWindow(Window, Tab->GetTabArgs().GetOwnerWindow(), false);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
