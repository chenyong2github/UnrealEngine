// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertServerSessionBrowser.h"

#include "ModalWindowManager.h"
#include "MultiUserServerModule.h"
#include "MultiUserServerUserSettings.h"
#include "Session/Browser/SConcertSessionBrowser.h"
#include "Widgets/Browser/ConcertServerSessionBrowserController.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

#include "Dialog/SMessageDialog.h"
#include "Framework/Docking/TabManager.h"
#include "Session/Browser/ConcertSessionItem.h"
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
		.OnLiveSessionDoubleClicked(InArgs._DoubleClickLiveSession)
		.OnArchivedSessionDoubleClicked(InArgs._DoubleClickArchivedSession)
		.OnRequestedDeleteSession(this, &SConcertServerSessionBrowser::RequestDeleteSession)
		// Pretend a modal dialog said no - RequestDeleteSession will show non-modal dialog
		.CanDeleteArchivedSession_Lambda([](TSharedPtr<FConcertSessionItem>) { return false; })
		.CanDeleteActiveSession_Lambda([](TSharedPtr<FConcertSessionItem>) { return false; })
		.ColumnVisibilitySnapshot(UMultiUserServerUserSettings::GetUserSettings()->GetSessionBrowserColumnVisibility())
		.SaveColumnVisibilitySnapshot_Lambda([](const FColumnVisibilitySnapshot& Snapshot)
		{
			UMultiUserServerUserSettings::GetUserSettings()->SetSessionBrowserColumnVisibility(Snapshot);
		});
}

void SConcertServerSessionBrowser::RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& SessionItem) 
{
	switch (SessionItem->Type)
	{
	case FConcertSessionItem::EType::ActiveSession:
		DeleteActiveSessionWithFakeModalQuestion(SessionItem); break;
	case FConcertSessionItem::EType::ArchivedSession:
		DeleteArchivedSessionWithFakeModalQuestion(SessionItem); break;
	default:
		break;
	}
}

void SConcertServerSessionBrowser::DeleteArchivedSessionWithFakeModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem)
{
	const FText Message = FText::Format(
	LOCTEXT("DeleteArchivedDescription", "Deleting a session will cause all associated data to be removed.\n\nDelete {0}?"),
		FText::FromString(SessionItem->SessionName)
		);

	auto DeleteArchived = [WeakController = TWeakPtr<FConcertServerSessionBrowserController>(Controller), WeakSessionItem = TWeakPtr<FConcertSessionItem>(SessionItem)]()
	{
		const TSharedPtr<FConcertServerSessionBrowserController> PinnedController = WeakController.Pin();
		const TSharedPtr<FConcertSessionItem> PinnedSessionItem = WeakSessionItem.Pin();
		if (PinnedController && PinnedSessionItem)
		{
			PinnedController->DeleteArchivedSession(PinnedSessionItem->ServerAdminEndpointId, PinnedSessionItem->SessionId);
		}
	};
	const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
		.Title(LOCTEXT("DisconnectUsersTitle", "Delete session?"))
		.IconBrush("Icons.WarningWithColor.Large")
		.Message(Message)
		.UseScrollBox(false)
		.Buttons({
			SMessageDialog::FButton(LOCTEXT("DeleteArchivedButton", "Delete"))
				.SetOnClicked(FSimpleDelegate::CreateLambda(DeleteArchived)),
			SMessageDialog::FButton(LOCTEXT("CancelButton", "Cancel"))
				.SetPrimary(true)
				.SetFocus()
		});

	UE::MultiUserServer::FConcertServerUIModule::Get()
		.GetModalWindowManager()
		->ShowFakeModalWindow(Dialog);
}

void SConcertServerSessionBrowser::DeleteActiveSessionWithFakeModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem)
{
	const int32 NumUsers = Controller.Pin()->GetNumConnectedClients(SessionItem->SessionId);
	const FText Message = FText::Format(
		LOCTEXT("DeletedActiveDescription", "There {0}|plural(one=is,other=are) {0} connected {0}|plural(one=client,other=clients) in the current session.\nDeleting a session will force all connected clients to disconnect.\n\nDelete {1}?"),
		NumUsers,
		FText::FromString(SessionItem->SessionName)
		);

	auto DeleteActive = [WeakController = TWeakPtr<FConcertServerSessionBrowserController>(Controller), WeakSessionItem = TWeakPtr<FConcertSessionItem>(SessionItem)]()
	{
		const TSharedPtr<FConcertServerSessionBrowserController> PinnedController = WeakController.Pin();
		const TSharedPtr<FConcertSessionItem> PinnedSessionItem = WeakSessionItem.Pin();
		if (PinnedController && PinnedSessionItem)
		{
			PinnedController->DeleteActiveSession(PinnedSessionItem->ServerAdminEndpointId, PinnedSessionItem->SessionId);
		}
	};
	const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
		.Title(LOCTEXT("DisconnectUsersTitle", "Force Users to Disconnect?"))
		.IconBrush("Icons.WarningWithColor.Large")
		.Message(Message)
		.UseScrollBox(false)
		.Buttons({
			SMessageDialog::FButton(LOCTEXT("DeleteActiveButton", "Delete"))
				.SetPrimary(true)
				.SetOnClicked(FSimpleDelegate::CreateLambda(DeleteActive))
				.SetFocus(),
			SMessageDialog::FButton(LOCTEXT("CancelButton", "Cancel"))
		});

	UE::MultiUserServer::FConcertServerUIModule::Get()
		.GetModalWindowManager()
		->ShowFakeModalWindow(Dialog);
}

#undef LOCTEXT_NAMESPACE
