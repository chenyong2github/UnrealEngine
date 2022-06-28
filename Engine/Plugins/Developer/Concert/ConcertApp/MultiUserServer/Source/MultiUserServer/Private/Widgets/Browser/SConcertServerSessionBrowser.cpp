// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertServerSessionBrowser.h"

#include "MultiUserServerModule.h"
#include "Session/Browser/ConcertBrowserUtils.h"
#include "Session/Browser/ConcertSessionItem.h"
#include "Session/Browser/SConcertSessionBrowser.h"
#include "Settings/MultiUserServerColumnVisibilitySettings.h"
#include "Widgets/Browser/ConcertServerSessionBrowserController.h"
#include "Window/ConcertServerTabs.h"
#include "Window/ModalWindowManager.h"

#include "Dialog/SMessageDialog.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SConcertServerSessionBrowser::Construct(const FArguments& InArgs, TSharedRef<FConcertServerSessionBrowserController> InController)
{
	Controller = InController;
	SConcertTabViewBase::Construct(
		SConcertTabViewBase::FArguments()
		.Content()
		[
			MakeSessionTableView(InArgs)
		],
		ConcertServerTabs::GetSessionBrowserTabId()
		);
}

TSharedRef<SWidget> SConcertServerSessionBrowser::MakeSessionTableView(const FArguments& InArgs)
{
	SearchText = MakeShared<FText>();
	return SAssignNew(SessionBrowser, SConcertSessionBrowser, Controller.Pin().ToSharedRef(), SearchText)
		.OnLiveSessionDoubleClicked(InArgs._DoubleClickLiveSession)
		.OnArchivedSessionDoubleClicked(InArgs._DoubleClickArchivedSession)
		.PostRequestedDeleteSession(this, &SConcertServerSessionBrowser::RequestDeleteSession)
		// Pretend a modal dialog said no - RequestDeleteSession will show non-modal dialog
		.AskUserToDeleteSessions_Lambda([](auto) { return false; })
		.ColumnVisibilitySnapshot(UMultiUserServerColumnVisibilitySettings::GetSettings()->GetSessionBrowserColumnVisibility())
		.SaveColumnVisibilitySnapshot_Lambda([](const FColumnVisibilitySnapshot& Snapshot)
		{
			UMultiUserServerColumnVisibilitySettings::GetSettings()->SetSessionBrowserColumnVisibility(Snapshot);
		});
}

void SConcertServerSessionBrowser::RequestDeleteSession(const TArray<TSharedPtr<FConcertSessionItem>>& SessionItems) 
{
	const FText Message = [this, &SessionItems]()
	{
		if (SessionItems.Num() > 1)
		{
			return FText::Format(
			LOCTEXT("DeletedMultipleDescription", "Deleting a session will force all connected clients to disconnect and all associated data to be removed.\n\nDelete {0} sessions?"),
				SessionItems.Num()
			);
		}
			
		switch (SessionItems[0]->Type)
		{
		case FConcertSessionItem::EType::ActiveSession:
			return FText::Format(
				LOCTEXT("DeletedActiveDescription", "There {0}|plural(one=is,other=are) {0} connected {0}|plural(one=client,other=clients) in the current session.\nDeleting a session will force all connected clients to disconnect.\n\nDelete {1}?"),
				Controller.Pin()->GetNumConnectedClients(SessionItems[0]->SessionId),
				FText::FromString(SessionItems[0]->SessionName)
			);
		case FConcertSessionItem::EType::ArchivedSession:
			return FText::Format(
				LOCTEXT("DeleteArchivedDescription", "Deleting a session will cause all associated data to be removed.\n\nDelete {0}?"),
				FText::FromString(SessionItems[0]->SessionName)
				);
		default:
			checkNoEntry();
			return FText::GetEmpty();
		}
	}();
	DeleteSessionsWithFakeModalQuestion(Message, SessionItems);
}

void SConcertServerSessionBrowser::DeleteSessionsWithFakeModalQuestion(const FText& Message, const TArray<TSharedPtr<FConcertSessionItem>>& SessionItems)
{
	auto DeleteArchived = [WeakController = TWeakPtr<FConcertServerSessionBrowserController>(Controller), SessionItems]()
	{
		if (const TSharedPtr<FConcertServerSessionBrowserController> PinnedController = WeakController.Pin())
		{
			ConcertBrowserUtils::RequestItemDeletion(*PinnedController.Get(), SessionItems);
		}
	};
	const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
		.Title(LOCTEXT("DisconnectUsersTitle", "Delete session?"))
		.Icon(FAppStyle::Get().GetBrush("Icons.WarningWithColor.Large"))
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

#undef LOCTEXT_NAMESPACE
