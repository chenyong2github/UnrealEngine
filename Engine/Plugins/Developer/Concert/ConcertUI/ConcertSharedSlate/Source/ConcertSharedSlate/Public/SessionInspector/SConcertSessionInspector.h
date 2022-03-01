// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FTabManager;
class SDockTab;
class SWindow;

/**
 * Designed to be the content of a tab showing:
 *  - activity history (transactions stored on the server as well as who made those transactions)
 *  - session content (list of session data saved during a Multi-user session)
 *  - connection monitor (details about the connected clients on the given session and network info)
 * Implements view in model-view-controller pattern.
 */
class CONCERTSHAREDSLATE_API SConcertSessionInspector : public SCompoundWidget
{
public:

	static const FName HistoryTabId;
	static const FName SessionContentTabId;
	static const FName ConnectionMonitorTabId;

	SLATE_BEGIN_ARGS(SConcertSessionInspector) {}
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedRef<SWindow>& ConstructUnderWindow);

private:
	
	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	// Spawn tabs
	TSharedRef<SWidget> CreateTabs(const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedRef<SWindow>& ConstructUnderWindow);
	TSharedRef<SDockTab> SpawnActivityHistory(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnSessionContent(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnConnectionMonitor(const FSpawnTabArgs& Args);
};
