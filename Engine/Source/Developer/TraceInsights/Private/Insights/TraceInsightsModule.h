// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Framework/Docking/TabManager.h"

namespace Trace
{
	class FStoreService;
	class IAnalysisService;
	class IModuleService;
}

class SDockTab;
class FSpawnTabArgs;
class SWindow;

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

	virtual void CreateDefaultStore() override;

	virtual Trace::FStoreClient* GetStoreClient() override;
	virtual bool ConnectToStore(const TCHAR* InStoreHost, uint32 InStorePort) override;

	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) override;
	virtual void CreateSessionViewer(bool bAllowDebugTools) override;

	virtual TSharedPtr<const Trace::IAnalysisSession> GetAnalysisSession() const override;
	virtual void StartAnalysisForTrace(uint32 InTraceId, bool InAutoQuit = false) override;
	virtual void StartAnalysisForLastLiveSession() override;
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile, bool InAutoQuit = false) override;

	virtual void ShutdownUserInterface() override;

	virtual void RegisterComponent(TSharedPtr<IInsightsComponent> Component) override;
	virtual void UnregisterComponent(TSharedPtr<IInsightsComponent> Component) override;

	virtual void RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig) override;
	virtual void UnregisterMajorTabConfig(const FName& InMajorTabId) override;
	virtual FOnInsightsMajorTabCreated& OnMajorTabCreated() override { return OnInsightsMajorTabCreatedDelegate; }
	virtual FOnRegisterMajorTabExtensions& OnRegisterMajorTabExtension(const FName& InMajorTabId) override;

	virtual const FInsightsMajorTabConfig& FindMajorTabConfig(const FName& InMajorTabId) const override;

	const FOnRegisterMajorTabExtensions* FindMajorTabLayoutExtension(const FName& InMajorTabId) const;

	/** Retrieve ini path for saving persistent layout data. */
	static const FString& GetUnrealInsightsLayoutIni();

	/** Set the ini path for saving persistent layout data. */
	virtual void SetUnrealInsightsLayoutIni(const FString& InIniPath) override;

	virtual void ScheduleCommand(const FString& InCmd) override;

	virtual void InitializeTesting(bool InInitAutomationModules, bool InAutoQuit) override;

protected:
	void InitTraceStore();

	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	void AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout);
	void AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools);

	/** Callback called when a major tab is closed. */
	void OnTabBeingClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Start Page */
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/** Session Info */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	void OnWindowClosedEvent(const TSharedRef<SWindow>&);

protected:
	TUniquePtr<Trace::FStoreService> StoreService;

	TSharedPtr<Trace::IAnalysisService> TraceAnalysisService;
	TSharedPtr<Trace::IModuleService> TraceModuleService;

	TMap<FName, FInsightsMajorTabConfig> TabConfigs;
	TMap<FName, FOnRegisterMajorTabExtensions> MajorTabExtensionDelegates;

	FOnInsightsMajorTabCreated OnInsightsMajorTabCreatedDelegate;

	TSharedPtr<FTabManager::FLayout> PersistentLayout;
	static FString UnrealInsightsLayoutIni;

	TArray<TSharedRef<IInsightsComponent>> Components;
};
