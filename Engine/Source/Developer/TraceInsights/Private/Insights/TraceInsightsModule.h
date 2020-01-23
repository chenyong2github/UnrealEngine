// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Framework/Docking/TabManager.h"

namespace Trace
{
	class FStoreService;
	class IAnalysisService;
	class ISessionService;
	class IModuleService;
}

class SDockTab;
class FSpawnTabArgs;

/**
 * Implements the Trace Insights module.
 */
class FTraceInsightsModule : public IUnrealInsightsModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual bool ConnectToStore(const TCHAR* Host, uint32 Port) override;
	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) override;
	virtual void CreateSessionViewer(bool bAllowDebugTools) override;
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile) override;
	virtual void StartAnalysisForTrace(const TCHAR* InStoreHost, uint32 InStorePort, uint32 InTraceId) override;
	virtual void StartAnalysisForLastLiveSession(/*StoreClient*/) override;
	//virtual void StartAnalysisForTrace(StoreClient or FTraceData..) override;
	virtual void ShutdownUserInterface() override;
	virtual void RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig) override;
	virtual void UnregisterMajorTabConfig(const FName& InMajorTabId) override;
	virtual FOnInsightsMajorTabCreated& OnMajorTabCreated() override { return OnInsightsMajorTabCreatedDelegate; }

	/** Find a major tab config for the specified ID */
	const FInsightsMajorTabConfig& FindMajorTabConfig(const FName& InMajorTabId) const;

protected:
	void InitTraceStore();

	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	void AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout);
	void AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools);

	/** Callback called when a major tab is closed. */
	void OnTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Timing Profiler major tab is closed. */
	void OnTimingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Loading Profiler major tab is closed. */
	void OnLoadingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Callback called when the Networking Profiler major tab is closed. */
	void OnNetworkingProfilerTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Start Page */
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/** Session Info */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	/** Timing Profiler */
	TSharedRef<SDockTab> SpawnTimingProfilerTab(const FSpawnTabArgs& Args);

	/** Loading Profiler */
	TSharedRef<SDockTab> SpawnLoadingProfilerTab(const FSpawnTabArgs& Args);

	/** Networking Profiler */
	TSharedRef<SDockTab> SpawnNetworkingProfilerTab(const FSpawnTabArgs& Args);

#if WITH_EDITOR
	/** Handle exit */
	void HandleExit();
#endif

protected:
	TUniquePtr<Trace::FStoreService> StoreService;

	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService;
	TSharedPtr<Trace::ISessionService> TraceSessionService;
	TSharedPtr<Trace::IModuleService> TraceModuleService;

	TMap<FName, FInsightsMajorTabConfig> TabConfigs;

	FOnInsightsMajorTabCreated OnInsightsMajorTabCreatedDelegate;

	TSharedPtr<FTabManager::FLayout> PersistentLayout;
	FString UnrealInsightsLayoutIni;
};
