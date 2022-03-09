// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertServerSessionBrowser.h"

#include "SessionBrowser/SConcertSessionBrowser.h"
#include "Widgets/Browser/ConcertServerSessionBrowserController.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

#include "SPositiveActionButton.h"
#include "Dialog/SMessageDialog.h"
#include "SessionBrowser/ConcertSessionItem.h"
#include "Widgets/ConcertServerTabs.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SConcertServerSessionBrowser::Construct(const FArguments& InArgs, TSharedRef<FConcertServerSessionBrowserController> InController)
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
		.OnSessionDoubleClicked(InArgs._DoubleClickSession)
		.CanArchiveSession(this, &SConcertServerSessionBrowser::ConfirmArchiveOperationWithDialog)
		.CanDeleteSession(this, &SConcertServerSessionBrowser::ConfirmDeleteOperationWithDialog);
}

bool SConcertServerSessionBrowser::ConfirmArchiveOperationWithDialog(TSharedPtr<FConcertSessionItem> SessionItem)
{
	const int32 NumUsers = Controller.Pin()->GetNumConnectedClients(SessionItem->SessionId);
	const FText Message = FText::Format(
		LOCTEXT("ArchiveDescription", "There {0}|plural(one=is,other=are) {0} connected {0}|plural(one=client,other=clients) in the current session.\nArchiving a session will force all connected clients to disconnect."),
		NumUsers
		);

	constexpr int32 ArchiveIndex = 0;
	const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
		.Title(LOCTEXT("DisconnectUsersTitle", "Force Users to Disconnect?"))
		.IconBrush("Icons.WarningWithColor.Large")
		.Message(Message)
		.Buttons({
			SMessageDialog::FButton(LOCTEXT("ArchiveButton", "Archive")).SetPrimary(true),
			SMessageDialog::FButton(LOCTEXT("CancelButton", "Cancel"))
		});
	return Dialog->ShowModal() == ArchiveIndex;
}

bool SConcertServerSessionBrowser::ConfirmDeleteOperationWithDialog(TSharedPtr<FConcertSessionItem> SessionItem)
{
	const FText Message = LOCTEXT("DeleteDescription", "Deleting a session will cause all associated data to be removed.");
	
	constexpr int32 DeleteIndex = 0;
	const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
		.Title(LOCTEXT("DisconnectUsersTitle", "Delete session?"))
		.IconBrush("Icons.WarningWithColor.Large")
		.Message(Message)
		.Buttons({
			SMessageDialog::FButton(LOCTEXT("DeleteButton", "Delete")),
			SMessageDialog::FButton(LOCTEXT("CancelButton", "Cancel")).SetPrimary(true)
		});
	return Dialog->ShowModal() == DeleteIndex;
}

#undef LOCTEXT_NAMESPACE
