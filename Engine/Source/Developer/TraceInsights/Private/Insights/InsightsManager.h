// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsSettings.h"
#include "Insights/IUnrealInsightsModule.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Trace
{
	class FStoreClient;
	class IAnalysisService;
	class IModuleService;
}

class SStartPageWindow;
class SSessionInfoWindow;
class FInsightsMessageLogViewModel;
class FInsightsTestRunner;
class FInsightsMenuBuilder;

/**
 * Utility class used by profiler managers to limit how often they check for availability conditions.
 */
class FAvailabilityCheck
{
public:
	/** Returns true if managers are allowed to do (slow) availability check during this tick. */
	bool Tick();

	/** Disables the "availability check" (i.e. Tick() calls will return false when disabled). */
	void Disable();

	/** Enables the "availability check" with a specified initial delay. */
	void Enable(double InWaitTime);

private:
	double WaitTime = 0.0;
	uint64 NextTimestamp = (uint64)-1;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages following areas:
 *     Connecting/disconnecting to source trace
 *     Global Unreal Insights application state and settings
 */
class FInsightsManager : public TSharedFromThis<FInsightsManager>, public IInsightsComponent
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
	static TSharedPtr<FInsightsManager> CreateInstance(TSharedRef<Trace::IAnalysisService> TraceAnalysisService,
													   TSharedRef<Trace::IModuleService> TraceModuleService);

	/** @return the global instance of the main manager (FInsightsManager). */
	static TSharedPtr<FInsightsManager> Get();

	//////////////////////////////////////////////////
	// IInsightsComponent

	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;

	//////////////////////////////////////////////////

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

	//////////////////////////////////////////////////
	// StartPage (Trace Store Browser)

	void AssignStartPageWindow(const TSharedRef<SStartPageWindow>& InStartPageWindow)
	{
		StartPageWindow = InStartPageWindow;
	}

	void RemoveStartPageWindow()
	{
		StartPageWindow.Reset();
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SStartPageWindow> GetStartPageWindow() const
	{
		return StartPageWindow.Pin();
	}

	//////////////////////////////////////////////////
	// Session Info

	void AssignSessionInfoWindow(const TSharedRef<SSessionInfoWindow>& InSessionInfoWindow)
	{
		SessionInfoWindow = InSessionInfoWindow;
	}

	void RemoveSessionInfoWindow()
	{
		SessionInfoWindow.Reset();
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SSessionInfoWindow> GetSessionInfoWindow() const
	{
		return SessionInfoWindow.Pin();
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
	 * @param InAutoQuit - The Application will close when session analysis is complete or fails to start
	 */
	void LoadTrace(uint32 TraceId, bool InAutoQuit = false);

	/**
	 * Creates a new analysis session instance and loads a trace file from the specified location.
	 * @param TraceFilename - The trace file to analyze
	 * @param InAutoQuit - The Application will close when session analysis is complete or fails to start
	 */
	void LoadTraceFile(const FString& TraceFilename, bool InAutoQuit = false);

	/** Opens the Settings dialog. */
	void OpenSettings();

	void UpdateSessionDuration();

	bool IsAnalysisComplete() const { return bIsAnalysisComplete; }
	double GetSessionDuration() const { return SessionDuration; }
	double GetAnalysisDuration() const { return AnalysisDuration; }
	double GetAnalysisSpeedFactor() const { return AnalysisSpeedFactor; }

	TSharedPtr<FInsightsMessageLogViewModel> GetMessageLog() { return InsightsMessageLogViewModel; }

	TSharedPtr<FInsightsMenuBuilder> GetInsightsMenuBuilder() { return InsightsMenuBuilder; }

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
	// SessionAnalysisCompletedEvent

public:
	/** The event to execute when session analysis is complete. */
	DECLARE_EVENT(FTimingProfilerManager, FSessionAnalysisCompletedEvent);
	FSessionAnalysisCompletedEvent& GetSessionAnalysisCompletedEvent() { return SessionAnalysisCompletedEvent; }
private:
	/** The event to execute when session analysis is completed. */
	FSessionAnalysisCompletedEvent SessionAnalysisCompletedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:
	/** Binds our UI commands to delegates. */
	void BindCommands();

	/** Called to spawn the Start Page major tab. */
	TSharedRef<SDockTab> SpawnStartPageTab(const FSpawnTabArgs& Args);

	/** Callback called when the Start Page major tab is closed. */
	void OnStartPageTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Called to spawn the Session Info major tab. */
	TSharedRef<SDockTab> SpawnSessionInfoTab(const FSpawnTabArgs& Args);

	/** Callback called when the Session Info major tab is closed. */
	void OnSessionInfoTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Called to spawn the Message Log major tab. */
	TSharedRef<SDockTab> SpawnMessageLogTab(const FSpawnTabArgs& Args);

	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

	/** Resets (closes) current session instance. */
	void ResetSession(bool bNotify = true);

	void OnSessionChanged();

	void SpawnAndActivateTabs();

	void ActivateTimingInsightsTab();

private:
	bool bIsInitialized;

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

	/** A weak pointer to the Session Info window. */
	TWeakPtr<class SSessionInfoWindow> SessionInfoWindow;

	TSharedPtr<class SWidget> InsightsMessageLog;
	TSharedPtr<FInsightsMessageLogViewModel> InsightsMessageLogViewModel;

	/** If enabled, UI can display additional info for debugging purposes. */
	bool bIsDebugInfoEnabled;

	bool bShouldOpenAnalysisInSeparateProcess;

	FStopwatch AnalysisStopwatch;
	bool bIsAnalysisComplete;
	double SessionDuration;
	double AnalysisDuration;
	double AnalysisSpeedFactor;

	bool bIsMainTabSet = false;

	TSharedPtr<FInsightsMenuBuilder> InsightsMenuBuilder;
	TSharedPtr<FInsightsTestRunner> TestRunner;

private:
	static const TCHAR* AutoQuitMsgOnFail;

	/** A shared pointer to the global instance of the main manager. */
	static TSharedPtr<FInsightsManager> Instance;
};
