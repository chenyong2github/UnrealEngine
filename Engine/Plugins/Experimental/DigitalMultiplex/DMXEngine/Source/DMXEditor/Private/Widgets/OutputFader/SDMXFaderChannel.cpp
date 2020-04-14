// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutputFader/SDMXFaderChannel.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFader.h"
#include "DMXEditorLog.h"

#include "Widgets/Common/SSpinBoxVertical.h"
#include "Widgets/OutputFader/SDMXOutputFaderList.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SDMXFaderChannel"

void SDMXFaderChannel::Construct(const FArguments& InArgs)
{
	WeakDMXEditor = InArgs._DMXEditor;
	UniverseNumber = InArgs._UniverseNumber;
	ChannelNumber = InArgs._ChannelNumber;


	ChildSlot
	.Padding(0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Fill)
		.Padding(0.f, 1.f, 0.f, 0.f)
		.AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			.FillHeight(10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				.FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(UniverseValue, STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::FromString("0"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				.FillWidth(1)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(ChannelValue, STextBlock)
						.Justification(ETextJustify::Center)
						.Text(FText::FromString("0"))
					]
				]
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			.FillHeight(1)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered"))
				.Padding(FMargin(0, 5, 0, 5))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
