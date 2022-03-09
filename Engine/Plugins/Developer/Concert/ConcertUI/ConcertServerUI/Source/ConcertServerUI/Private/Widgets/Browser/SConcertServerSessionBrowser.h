// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SessionBrowser/SConcertSessionBrowser.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FConcertServerSessionBrowserController;
class SSearchBox;

/** Shows a list of server sessions */
class SConcertServerSessionBrowser : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SConcertServerSessionBrowser) { }
		SLATE_EVENT(FSessionDelegate, DoubleClickSession)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FConcertServerSessionBrowserController> InController);

	void RefreshSessionList() { SessionBrowser->RefreshSessionList(); }
	
private:

	/** We can ask the controller about information and notify it about UI events. */
	TWeakPtr<FConcertServerSessionBrowserController> Controller;

	TSharedPtr<FText> SearchText;
	TSharedPtr<SConcertSessionBrowser> SessionBrowser;
	
	TSharedRef<SWidget> MakeSessionTableView(const FArguments& InArgs);

	bool ConfirmArchiveOperationWithDialog(TSharedPtr<FConcertSessionItem> SessionItem);
	bool ConfirmDeleteOperationWithDialog(TSharedPtr<FConcertSessionItem> SessionItem);
};
