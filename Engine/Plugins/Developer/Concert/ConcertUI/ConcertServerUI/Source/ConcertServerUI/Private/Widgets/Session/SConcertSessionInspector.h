// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServerSessionHistoryController.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FTabManager;
class FSpawnTabArgs;
class SDockTab;
class SWindow;

/**
 * Designed to be the content of a tab showing:
 *  - activity history (transactions stored on the server as well as who made those transactions)
 *  - session content (list of session data saved during a Multi-user session)
 *  - connection monitor (details about the connected clients on the given session and network info)
 * Implements view in model-view-controller pattern.
 */
class SConcertSessionInspector : public SCompoundWidget
{
public:

	struct FRequiredArgs
	{
		TSharedRef<SDockTab> ConstructUnderMajorTab;
		TSharedRef<SWindow> ConstructUnderWindow;
		TSharedRef<FServerSessionHistoryController> SessionHistoryController;

		FRequiredArgs(TSharedRef<SDockTab> ConstructUnderMajorTab, TSharedRef<SWindow> ConstructUnderWindow,TSharedRef<FServerSessionHistoryController> SessionHistoryController)
			: ConstructUnderMajorTab(MoveTemp(ConstructUnderMajorTab))
			, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
			, SessionHistoryController(MoveTemp(SessionHistoryController))
		{}
	};

	static const FName HistoryTabId;
	static const FName SessionContentTabId;
	static const FName ConnectionMonitorTabId;

	SLATE_BEGIN_ARGS(SConcertSessionInspector) {}
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FRequiredArgs& RequiredArgs);

private:
	
	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	// Spawn tabs
	TSharedRef<SWidget> CreateTabs(const FArguments& InArgs, const FRequiredArgs& RequiredArgs);
	TSharedRef<SDockTab> SpawnActivityHistory(const FSpawnTabArgs& Args, TSharedRef<FServerSessionHistoryController> SessionHistoryController);
	TSharedRef<SDockTab> SpawnSessionContent(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnConnectionMonitor(const FSpawnTabArgs& Args);
};
