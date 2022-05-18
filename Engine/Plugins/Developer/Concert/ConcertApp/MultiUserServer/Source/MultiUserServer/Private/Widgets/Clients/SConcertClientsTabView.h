// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SConcertTabViewWithManagerBase.h"

struct FConcertClientInfo;
class IConcertSyncServer;
class FConcertLogTokenizer;
class FTabManager;
class SDockTab;
class SPromptConcertLoggingEnabled;
class SWidget;
class SWindow;

/** Manages the UI logic of the Clients tab */
class SConcertClientsTabView : public SConcertTabViewWithManagerBase
{
public:
	
	static const FName ClientBrowserTabId;
	static const FName GlobalLogTabId;

	SLATE_BEGIN_ARGS(SConcertClientsTabView)
	{}
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, ConstructUnderMajorTab)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ConstructUnderWindow)
	SLATE_END_ARGS()
	virtual ~SConcertClientsTabView() override;

	void Construct(const FArguments& InArgs, FName InStatusBarID, TSharedRef<IConcertSyncServer> InServer);

private:

	/** Used to overlay EnableLoggingPrompt over the tabs */
	TSharedPtr<SOverlay> EnableLoggingPromptOverlay;
	/** Reminds the user to enable logging */
	TSharedPtr<SPromptConcertLoggingEnabled> EnableLoggingPrompt;

	/** Used by various systems to convert logs to text */
	TSharedPtr<FConcertLogTokenizer> LogTokenizer;

	/** Used to look up client info */
	TSharedPtr<IConcertSyncServer> Server;
	
	void CreateTabs(const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout, const FArguments& InArgs);
	TSharedRef<SDockTab> SpawnClientBrowserTab(const FSpawnTabArgs& InTabArgs);
	TSharedRef<SDockTab> SpawnGlobalLogTab(const FSpawnTabArgs& InTabArgs);

	TSharedRef<SWidget> SetupLoggingPromptOverlay(const TSharedRef<SWidget>& TabsWidget);
	void OnConcertLoggingEnabledChanged(bool bNewEnabled);
};
