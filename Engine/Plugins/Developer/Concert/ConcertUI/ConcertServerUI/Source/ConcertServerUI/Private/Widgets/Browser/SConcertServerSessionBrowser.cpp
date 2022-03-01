// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertServerSessionBrowser.h"

#include "SessionBrowser/SConcertSessionBrowser.h"
#include "Widgets/Browser/ConcertServerSessionBrowserController.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

#include "SPositiveActionButton.h"
#include "SessionBrowser/ConcertSessionItem.h"
#include "Widgets/ConcertServerTabs.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SConcertServerSessionBrowser::Construct(const FArguments& InArgs, TSharedRef<IConcertSessionBrowserController> InController)
{
	Controller = InController;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(1.0f, 2.0f))
		[
			SNew(SVerticalBox)

			// Session list.
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(1.0f, 2.0f)
			[
				MakeSessionTableView(InArgs)
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SConcertStatusBar, ConcertServerTabs::GetSessionBrowserTabId())
			]
		]
	];
}

TSharedRef<SWidget> SConcertServerSessionBrowser::MakeSessionTableView(const FArguments& InArgs)
{
	SearchText = MakeShared<FText>();
	return SAssignNew(SessionBrowser, SConcertSessionBrowser, Controller.Pin().ToSharedRef(), SearchText)
		.OnSessionDoubleClicked(InArgs._DoubleClickSession);
}

#undef LOCTEXT_NAMESPACE
