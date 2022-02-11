// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertServerSessionBrowser.h"

#include "SPositiveActionButton.h"
#include "OutputLog/Public/OutputLogModule.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SConcertServerSessionBrowser::Construct(const FArguments& InArgs, TWeakPtr<IConcertServerSessionBrowserController> InController)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(1.0f, 2.0f))
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeControlBar()
			]

			// Session list.
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(1.0f, 2.0f)
			[
				MakeSessionTableView()
			]
		]
	];
}

TSharedRef<SWidget> SConcertServerSessionBrowser::MakeControlBar()
{
	return SNew(SHorizontalBox)

		// + New Session
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SPositiveActionButton)
				.OnClicked(this, &SConcertServerSessionBrowser::OnNewSessionClicked)
				.Text(LOCTEXT("NewSession", "New Session"))
		]

		// The search text.
		+SHorizontalBox::Slot()
		.FillWidth(1.0)
		.Padding(4.0f, 3.0f, 8.0f, 3.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search sessions"))
			.OnTextChanged(this, &SConcertServerSessionBrowser::OnSearchTextChanged)
			.OnTextCommitted(this, &SConcertServerSessionBrowser::OnSearchTextCommitted)
			.DelayChangeNotificationsWhileTyping(true)
		]
	;
}

TSharedRef<SWidget> SConcertServerSessionBrowser::MakeSessionTableView()
{
	//return FOutputLogModule::Get().MakeOutputLogDrawerWidget(FSimpleDelegate());
	return SNullWidget::NullWidget;
}

FReply SConcertServerSessionBrowser::OnNewSessionClicked()
{
	// TODO:
	return FReply::Handled();
}

void SConcertServerSessionBrowser::OnSearchTextChanged(const FText& InFilterText)
{
	// TODO:
}

void SConcertServerSessionBrowser::OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType)
{
	// TODO:
}

#undef LOCTEXT_NAMESPACE
