// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class IConcertServerSessionBrowserController;
class SSearchBox;

/** Shows a list of server sessions */
class SConcertServerSessionBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConcertServerSessionBrowser) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<IConcertServerSessionBrowserController> InController);

private:

	/** We can ask the controller about information and notify it about UI events. */
	TWeakPtr<IConcertServerSessionBrowserController> Controller;
	
	TSharedPtr<SSearchBox> SearchBox;
	
	TSharedRef<SWidget> MakeControlBar();
	TSharedRef<SWidget> MakeSessionTableView();

	FReply OnNewSessionClicked();

	void OnSearchTextChanged(const FText& InFilterText);
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);
};
