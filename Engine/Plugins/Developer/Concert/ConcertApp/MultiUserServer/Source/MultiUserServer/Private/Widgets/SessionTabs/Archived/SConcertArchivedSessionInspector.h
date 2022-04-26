// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Session/History/SEditableSessionHistory.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

class SSessionHistory;

/** Designed as content for a tab. Displays information about an archived session. */
class SConcertArchivedSessionInspector : public SCompoundWidget
{
public:
	
	static const FName HistoryTabId;

	SLATE_BEGIN_ARGS(SConcertArchivedSessionInspector)
	{}
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, ConstructUnderMajorTab)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ConstructUnderWindow)
		SLATE_EVENT(SEditableSessionHistory::FMakeSessionHistory, MakeSessionHistory)
		SLATE_EVENT(SEditableSessionHistory::FCanDeleteActivities, CanDeleteActivity)
		SLATE_EVENT(SEditableSessionHistory::FRequestDeleteActivities, DeleteActivity)
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	
	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	TSharedPtr<SEditableSessionHistory> SessionHistory;
	
	TSharedRef<SWidget> CreateTabs(const FArguments& InArgs);
	TSharedRef<SDockTab> SpawnActivityHistory(const FSpawnTabArgs& Args,
		SEditableSessionHistory::FMakeSessionHistory FMakeSessionHistory,
		SEditableSessionHistory::FCanDeleteActivities CanDeleteActivity,
		SEditableSessionHistory::FRequestDeleteActivities DeleteActivity
		);
};
