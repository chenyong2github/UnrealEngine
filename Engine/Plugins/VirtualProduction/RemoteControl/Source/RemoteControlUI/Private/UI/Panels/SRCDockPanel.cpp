// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Panels/SRCDockPanel.h"

#include "SlateOptMacros.h"
#include "Styling/RemoteControlStyles.h"

#include "UI/RemoteControlPanelStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

SLATE_IMPLEMENT_WIDGET(SRCMajorPanel)

void SRCMajorPanel::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCMajorPanel::Construct(const SRCMajorPanel::FArguments& InArgs)
{
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	bIsFooterEnabled = InArgs._EnableFooter;
	bIsHeaderEnabled = InArgs._EnableHeader;

	ChildSlot
	.Padding(RCPanelStyle->PanelPadding)
	[
		SNew(SBorder)
		.BorderImage(&RCPanelStyle->ContentAreaBrush)
		.Padding(RCPanelStyle->PanelPadding)
		[	
			SAssignNew(ContentPanel, SSplitter)
			.Orientation(InArgs._Orientation)

			// Content Panel
			+SSplitter::Slot()
			.Value(1.f)
			[
				SAssignNew(Children, SSplitter)
				.Orientation(InArgs._ChildOrientation)
			]
		]
	];

	// Enable Header upon explicit request.
	if (bIsHeaderEnabled.Get() && ContentPanel.IsValid())
	{
		// Header Panel
		ContentPanel->AddSlot(0)
			.Resizable(false)
			.SizeRule(SSplitter::SizeToContent)
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.f, 4.f)
				[
					SNew(STextBlock)
					.TextStyle(&RCPanelStyle->HeaderTextStyle)
					.Text(InArgs._HeaderLabel)
				]
			];
	}

	// Enable Footer upon explicit request.
	if (bIsFooterEnabled.Get() && ContentPanel.IsValid())
	{
		// Footer Panel
		ContentPanel->AddSlot(bIsHeaderEnabled.Get() ? 2 : 1)
			.Resizable(false)
			.SizeRule(SSplitter::SizeToContent)
			[
				SNew(SHorizontalBox)

				// Left Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 4.f, 2.f, 4.f)
				[
					SAssignNew(LeftToolbar, SHorizontalBox)
				]

				// Spacer.
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
				]
				
				// Right Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 4.f, 4.f, 4.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(RightToolbar, SHorizontalBox)
				]
			];
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCMajorPanel::AddPanel(TSharedRef<SWidget> InContent, const float InDesiredSize)
{
	if (Children.IsValid())
	{
		Children->AddSlot()
			.Value(InDesiredSize)
			[
				InContent
			];
	}
}

const TSharedRef<SWidget>& SRCMajorPanel::GetContent() const
{
	return ChildSlot.GetWidget();
}

void SRCMajorPanel::ClearContent()
{
	ChildSlot.DetachWidget();
}

void SRCMajorPanel::AddFooterToolItem(EToolbar InToolbar, TSharedRef<SWidget> InWidget)
{
	switch (InToolbar)
	{
		case Left:
			if (LeftToolbar.IsValid() && bIsFooterEnabled.Get())
			{
				LeftToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		case Right:
			if (RightToolbar.IsValid() && bIsFooterEnabled.Get())
			{
				RightToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		default:
			break;
	}
}

SLATE_IMPLEMENT_WIDGET(SRCMinorPanel)

void SRCMinorPanel::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCMinorPanel::Construct(const SRCMinorPanel::FArguments& InArgs)
{
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	bIsFooterEnabled = InArgs._EnableFooter;
	bIsHeaderEnabled = InArgs._EnableHeader;

	ChildSlot
	.Padding(RCPanelStyle->PanelPadding)
	[
		SNew(SBorder)
		.BorderImage(&RCPanelStyle->ContentAreaBrush)
		.Padding(RCPanelStyle->PanelPadding)
		[	
			SAssignNew(ContentPanel, SSplitter)
			.Orientation(InArgs._Orientation)

			// Content Panel
			+SSplitter::Slot()
			.Value(1.f)
			[
				InArgs._Content.Widget
			]
		]
	];

	// Enable Header upon explicit request.
	if (bIsHeaderEnabled.Get() && ContentPanel.IsValid())
	{
		// Header Panel
		ContentPanel->AddSlot(0)
			.SizeRule(SSplitter::SizeToContent)
			.Resizable(false)
			[
				SNew(SHorizontalBox)

				// Left Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 4.f, 2.f, 4.f)
				[
					SAssignNew(LeftHeaderToolbar, SHorizontalBox)
				]

				// Header Label.
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(2.f, 4.f)
				[
					SNew(STextBlock)
					.TextStyle(&RCPanelStyle->HeaderTextStyle)
					.Text(InArgs._HeaderLabel)
				]
				
				// Right Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 4.f, 4.f, 4.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(RightHeaderToolbar, SHorizontalBox)
				]
			];
	}
	
	// Enable Footer upon explicit request.
	if (bIsFooterEnabled.Get() && ContentPanel.IsValid())
	{
		// Footer Panel
		ContentPanel->AddSlot(bIsHeaderEnabled.Get() ? 2 : 1)
			.SizeRule(SSplitter::SizeToContent)
			.Resizable(false)
			[
				SNew(SHorizontalBox)

				// Left Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 4.f, 2.f, 4.f)
				[
					SAssignNew(LeftFooterToolbar, SHorizontalBox)
				]

				// Spacer.
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
				]
				
				// Right Toolbar
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 4.f, 4.f, 4.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(RightFooterToolbar, SHorizontalBox)
				]
			];
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCMinorPanel::SetContent(TSharedRef<SWidget> InContent)
{
	ChildSlot
		[
			InContent
		];
}

const TSharedRef<SWidget>& SRCMinorPanel::GetContent() const
{
	return ChildSlot.GetWidget();
}

void SRCMinorPanel::ClearContent()
{
	ChildSlot.DetachWidget();
}

void SRCMinorPanel::AddFooterToolbarItem(EToolbar InToolbar, TSharedRef<SWidget> InWidget)
{
	switch (InToolbar)
	{
		case Left:
			if (LeftFooterToolbar.IsValid() && bIsFooterEnabled.Get())
			{
				LeftFooterToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		case Right:
			if (RightFooterToolbar.IsValid() && bIsFooterEnabled.Get())
			{
				RightFooterToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		default:
			break;
	}
}

void SRCMinorPanel::AddHeaderToolbarItem(EToolbar InToolbar, TSharedRef<SWidget> InWidget)
{
	switch (InToolbar)
	{
		case Left:
			if (LeftHeaderToolbar.IsValid() && bIsHeaderEnabled.Get())
			{
				LeftHeaderToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		case Right:
			if (RightHeaderToolbar.IsValid() && bIsHeaderEnabled.Get())
			{
				RightHeaderToolbar->AddSlot()
					.Padding(5.f, 0.f)
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						InWidget
					];
			}
			break;
		default:
			break;
	}
}
