// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/History/SSessionHistory.h"
#include "PackageViewer/SConcertSessionPackageViewer.h"
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
		TSharedRef<SSessionHistory> SessionHistory;
		TSharedRef<SConcertSessionPackageViewer> PackageViewer;

		FRequiredArgs(TSharedRef<SDockTab> ConstructUnderMajorTab, TSharedRef<SWindow> ConstructUnderWindow,
				TSharedRef<SSessionHistory> SessionHistoryController,
				TSharedRef<SConcertSessionPackageViewer> PackageViewerController)
			: ConstructUnderMajorTab(MoveTemp(ConstructUnderMajorTab))
			, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
			, SessionHistory(MoveTemp(SessionHistoryController))
			, PackageViewer(MoveTemp(PackageViewerController))
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
	TSharedRef<SDockTab> SpawnActivityHistory(const FSpawnTabArgs& Args, TSharedRef<SSessionHistory> SessionHistory);
	TSharedRef<SDockTab> SpawnSessionContent(const FSpawnTabArgs& Args, TSharedRef<SConcertSessionPackageViewer> PackageViewer);
	TSharedRef<SDockTab> SpawnConnectionMonitor(const FSpawnTabArgs& Args);
};
