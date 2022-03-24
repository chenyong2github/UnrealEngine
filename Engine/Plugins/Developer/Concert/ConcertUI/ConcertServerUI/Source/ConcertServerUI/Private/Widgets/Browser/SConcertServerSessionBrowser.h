// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/Browser/SConcertSessionBrowser.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMessageDialog;
class FConcertServerSessionBrowserController;
class SSearchBox;

/** Shows a list of server sessions */
class SConcertServerSessionBrowser : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SConcertServerSessionBrowser) { }
		SLATE_EVENT(FSessionDelegate, DoubleClickLiveSession)
		SLATE_EVENT(FSessionDelegate, DoubleClickArchivedSession)
	SLATE_END_ARGS()
	virtual ~SConcertServerSessionBrowser() override;

	void Construct(const FArguments& InArgs, TSharedRef<FConcertServerSessionBrowserController> InController);

	void RequestRefreshListNextTick() { bRequestedRefresh = true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (bRequestedRefresh)
		{
			SessionBrowser->RefreshSessionList();
			bRequestedRefresh = false;
		}

		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
	
private:

	/** We can ask the controller about information and notify it about UI events. */
	TWeakPtr<FConcertServerSessionBrowserController> Controller;
	/** Tracks whether there is a dialog asking the user to delete a session. Used to avoid opening multiple. */
	TWeakPtr<SMessageDialog> DeleteSessionDialog;

	bool bRequestedRefresh = false;

	TSharedPtr<FText> SearchText;
	TSharedPtr<SConcertSessionBrowser> SessionBrowser;
	
	TSharedRef<SWidget> MakeSessionTableView(const FArguments& InArgs);

	void RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& SessionItem);
	void OnRootWindowClosed(const TSharedRef<SWindow>&) const;
	void UnregisterFromOnRootWindowClosed() const;
	void DeleteArchivedSessionWithNonModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem);
	void DeleteActiveSessionWithNonModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem);
};
