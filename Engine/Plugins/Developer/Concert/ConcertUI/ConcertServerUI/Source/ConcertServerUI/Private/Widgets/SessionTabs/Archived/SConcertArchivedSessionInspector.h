// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"

/** Designed as content for a tab. Displays information about an archived session. */
class SConcertArchivedSessionInspector : public SCompoundWidget
{
public:

	struct FRequiredArgs
	{
		TSharedRef<SDockTab> ConstructUnderMajorTab;
		TSharedRef<SWindow> ConstructUnderWindow;

		FRequiredArgs(TSharedRef<SDockTab> ConstructUnderMajorTab, TSharedRef<SWindow> ConstructUnderWindow)
			: ConstructUnderMajorTab(MoveTemp(ConstructUnderMajorTab))
			, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
		{}
	};
	
	static const FName HistoryTabId;

	SLATE_BEGIN_ARGS(SConcertArchivedSessionInspector)
	{}
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FRequiredArgs& InRequiredArgs);

private:
	
	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;
	
	TSharedRef<SWidget> CreateTabs(const FRequiredArgs& RequiredArgs);
	TSharedRef<SDockTab> SpawnActivityHistory(const FSpawnTabArgs& Args);
};
