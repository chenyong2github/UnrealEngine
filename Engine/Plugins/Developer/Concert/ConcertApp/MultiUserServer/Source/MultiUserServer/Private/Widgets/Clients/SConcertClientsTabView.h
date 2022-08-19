// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SConcertTabViewWithManagerBase.h"

namespace UE
{
	namespace MultiUserServer
	{
		class SConcertNetworkBrowser;
	}
}

class FConcertClientsTabController;
class FConcertLogTokenizer;
class FEndpointToUserNameCache;
class FGlobalLogSource;
class FTabManager;
class IConcertSyncServer;
class SDockTab;
class SPromptConcertLoggingEnabled;
class SWidget;
class SWindow;
struct FConcertClientInfo;

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

	void Construct(const FArguments& InArgs, FName InStatusBarID, TSharedRef<IConcertSyncServer> InServer, TSharedRef<FGlobalLogSource> InLogBuffer);
	
	void ShowConnectedClients(const FGuid& SessionId) const;
	void OpenGlobalLogTab() const;
	void CloseGlobalLogTab() const;
	void OpenClientLogTab(const FGuid& ClientMessageNodeId) const;
	
	bool IsGlobalLogOpen() const;
	TSharedPtr<SDockTab> GetGlobalLogTab() const;
	
private:

	/** Used to look up client info */
	TSharedPtr<IConcertSyncServer> Server;
	/** Buffers all logs globally */
	TSharedPtr<FGlobalLogSource> LogBuffer;

	/** Caches client info so it remains available even after a client disconnects */
	TSharedPtr<FEndpointToUserNameCache> ClientInfoCache;
	/** Used by various systems to convert logs to text */
	TSharedPtr<FConcertLogTokenizer> LogTokenizer;

	TSharedPtr<UE::MultiUserServer::SConcertNetworkBrowser> ClientBrowser;
	
	/** Used to overlay EnableLoggingPrompt over the tabs */
	TSharedPtr<SOverlay> EnableLoggingPromptOverlay;
	/** Reminds the user to enable logging */
	TSharedPtr<SPromptConcertLoggingEnabled> EnableLoggingPrompt;
	
	void CreateTabs(const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout, const FArguments& InArgs);
	TSharedRef<SDockTab> SpawnClientBrowserTab(const FSpawnTabArgs& InTabArgs);
	TSharedRef<SDockTab> SpawnGlobalLogTab(const FSpawnTabArgs& InTabArgs);

	TSharedRef<SWidget> CreateOpenGlobalLogButton() const;
};
