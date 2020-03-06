// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Trace
{
	class FStoreClient;
}

class SStartPageWindow;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages following areas:
 *     Connecting/disconnecting to source trace
 *     Lifetime of all other managers (specific profilers)
 *     Global Unreal Insights application state and settings
 */
class FInsightsManager
	: public TSharedFromThis<FInsightsManager>
{
	friend class FInsightsActionManager;

public:
	/** Creates the main manager, only one instance can exist. */
	FInsightsManager(TSharedRef<Trace::IAnalysisService> TraceAnalysisService,
					 TSharedRef<Trace::IModuleService> TraceModuleService);

	/** Virtual destructor. */
	virtual ~FInsightsManager();

	/**
	 * Creates an instance of the main manager and initializes global instance with the previously created instance of the manager.
	 * @param TraceAnalysisService The trace analysis service
	 * @param TraceModuleService   The trace module service
	 */
	static TSharedPtr<FInsightsManager> Initialize(TSharedRef<Trace::IAnalysisService> TraceAnalysisService,
												   TSharedRef<Trace::IModuleService> TraceModuleService)
	{
		if (FInsightsManager::Instance.IsValid())
		{
			FInsightsManager::Instance.Reset();
		}

		FInsightsManager::Instance = MakeShareable(new FInsightsManager(TraceAnalysisService, TraceModuleService));
		FInsightsManager::Instance->PostConstructor();

		return FInsightsManager::Instance;
	}

	/** Shutdowns the main manager. */
	void Shutdown()
	{
		if (FInsightsManager::Instance.IsValid())
		{
			FInsightsManager::Instance.Reset();
		}
	}

	/** @return the global instance of the main manager (FInsightsManager). */
	static TSharedPtr<FInsightsManager> Get();

	TSharedRef<Trace::IAnalysisService> GetAnalysisService() const { return AnalysisService; }
	TSharedRef<Trace::IModuleService> GetModuleService() const { return ModuleService; }

	void SetStoreDir(const FString& InStoreDir) { StoreDir = InStoreDir; }
	const FString& GetStoreDir() const { return StoreDir; }

	bool ConnectToStore(const TCHAR* Host, uint32 Port);
	Trace::FStoreClient* GetStoreClient() const { return StoreClient.Get(); }

	/** @return an instance of the trace analysis session. */
	TSharedPtr<const Trace::IAnalysisSession> GetSession() const;

	/** @return the id of the trace being analyzed. */
	uint32 GetTraceId() const { return CurrentTraceId; }

	/** @return the filename of the trace being analyzed. */
	const FString& GetTraceFilename() const { return CurrentTraceFilename; }

	/** @returns UI command list for the main manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the main commands. */
	static const FInsightsCommands& GetCommands();

	/** @return an instance of the main action manager. */
	static FInsightsActionManager& GetActionManager();

	/** @return an instance of the main settings. */
	static FInsightsSettings& GetSettings();

	void AssignStartPageWindow(const TSharedRef<SStartPageWindow>& InStartPageWindow)
	{
		StartPageWindow = InStartPageWindow;
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SStartPageWindow> GetStartPageWindow() const
	{
		return StartPageWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if UI is allowed to display debug info. */
	const bool IsDebugInfoEnabled() const { return bIsDebugInfoEnabled; }
	void SetDebugInfo(const bool bDebugInfoEnabledState) { bIsDebugInfoEnabled = bDebugInfoEnabledState; }

	////////////////////////////////////////////////////////////////////////////////////////////////////

	bool ShouldOpenAnalysisInSeparateProcess() const { return bShouldOpenAnalysisInSeparateProcess; }
	void SetOpenAnalysisInSeparateProcess(bool bOnOff) { bShouldOpenAnalysisInSeparateProcess = bOnOff; }

	/** Creates a new analysis session instance and loads the latest available trace that is live. */
	void LoadLastLiveSession();

	/**
	 * Creates a new analysis session instance using specified trace id.
	 * @param TraceId - The id of the trace to analyze
	 */
	void LoadTrace(uint32 TraceId);

	/**
	 * Creates a new analysis session instance and loads a trace file from the specified location.
	 * @param TraceFilename - The trace file to analyze
	 */
	void LoadTraceFile(const FString& TraceFilename);

	/** Opens the Settings dialog. */
	void OpenSettings();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// SessionChangedEvent

public:
	/** The event to execute when the session has changed. */
	DECLARE_EVENT(FTimingProfilerManager, FSessionChangedEvent);
	FSessionChangedEvent& GetSessionChangedEvent() { return SessionChangedEvent; }
private:
	/** The event to execute when the session has changed. */
	FSessionChangedEvent SessionChangedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:
	/** Finishes initialization of the profiler manager. */
	void PostConstructor();

	/** Binds our UI commands to delegates. */
	void BindCommands();

	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

	/** Resets (closes) current session instance. */
	void ResetSession(bool bNotify = true);

	void OnSessionChanged();

	void SpawnAndActivateTabs();

	void ActivateTimingInsightsTab();

private:
	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	TSharedRef<Trace::IAnalysisService> AnalysisService;
	TSharedRef<Trace::IModuleService> ModuleService;

	/** The location of the trace files managed by the trace store. */
	FString StoreDir;

	/** The client used to connect to the trace store. */
	TUniquePtr<Trace::FStoreClient> StoreClient;

	/** The trace analysis session. */
	TSharedPtr<const Trace::IAnalysisSession> Session;

	/** The id of the trace being analyzed. */
	uint32 CurrentTraceId;

	/** The filename of the trace being analyzed. */
	FString CurrentTraceFilename;

	/** List of UI commands for this manager. This will be filled by this and corresponding classes. */
	TSharedRef<FUICommandList> CommandList;

	/** An instance of the main action manager. */
	FInsightsActionManager ActionManager;

	/** An instance of the main settings. */
	FInsightsSettings Settings;

	/** A weak pointer to the Start Page window. */
	TWeakPtr<class SStartPageWindow> StartPageWindow;

	/** If enabled, UI can display additional info for debugging purposes. */
	bool bIsDebugInfoEnabled;

	/** A shared pointer to the global instance of the main manager. */
	static TSharedPtr<FInsightsManager> Instance;

	bool bIsNetworkingProfilerAvailable;

	bool bShouldOpenAnalysisInSeparateProcess;
};
