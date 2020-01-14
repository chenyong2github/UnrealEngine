// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class SStartPageWindow;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FInsightsManagerTabs
{
	static const FName StartPageTabId;
	static const FName SessionInfoTabId;
	static const FName TimingProfilerTabId;
	static const FName LoadingProfilerTabId;
	static const FName NetworkingProfilerTabId;
};

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
					 TSharedRef<Trace::ISessionService> SessionService,
					 TSharedRef<Trace::IModuleService> TraceModuleService);

	/** Virtual destructor. */
	virtual ~FInsightsManager();

	/**
	 * Creates an instance of the main manager and initializes global instance with the previously created instance of the manager.
	 * @param TraceAnalysisService The trace analysis service
	 * @param TraceSessionService  The trace session service
	 * @param TraceModuleService   The trace module service
	 */
	static TSharedPtr<FInsightsManager> Initialize(TSharedRef<Trace::IAnalysisService> TraceAnalysisService,
												   TSharedRef<Trace::ISessionService> TraceSessionService,
												   TSharedRef<Trace::IModuleService> TraceModuleService)
	{
		if (FInsightsManager::Instance.IsValid())
		{
			FInsightsManager::Instance.Reset();
		}

		FInsightsManager::Instance = MakeShareable(new FInsightsManager(TraceAnalysisService, TraceSessionService, TraceModuleService));
		FInsightsManager::Instance->PostConstructor();

		return FInsightsManager::Instance;
	}

	/** Shutdowns the main manager. */
	void Shutdown()
	{
		FInsightsManager::Instance.Reset();
	}

	/** @return the global instance of the main manager (FInsightsManager). */
	static TSharedPtr<FInsightsManager> Get();

	TSharedRef<Trace::IAnalysisService> GetAnalysisService() const { return AnalysisService; }
	TSharedRef<Trace::ISessionService> GetSessionService() const { return SessionService; }
	TSharedRef<Trace::IModuleService> GetModuleService() const { return ModuleService; }

	/** @return an instance of the trace analysis session. */
	TSharedPtr<const Trace::IAnalysisSession> GetSession() const;

	/** @return the session handle of the trace analysis session. */
	Trace::FSessionHandle GetSessionHandle() const;

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

	bool IsAnyLiveSessionAvailable(Trace::FSessionHandle& OutLastLiveSessionHandle) const;
	bool IsAnySessionAvailable(Trace::FSessionHandle& OutLastSessionHandle) const;

	/** Creates a new analysis session instance and loads the latest available trace session that is live. */
	void LoadLastLiveSession();

	/** Creates a new analysis session instance and loads the latest available trace session. */
	void LoadLastSession();

	/**
	 * Creates a new analysis session instance using specified session handle.
	 * @param SessionHandle - The handle for session to analyze
	 */
	void LoadSession(Trace::FSessionHandle SessionHandle);

	/**
	 * Creates a new analysis session instance and loads a trace file from the specified location.
	 * @param TraceFilepath - The path to the trace file
	 */
	void LoadTraceFile(const FString& TraceFilepath);

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
	void ResetSession();

	void OnSessionChanged();

	void SpawnAndActivateTabs();

	void ActivateTimingInsightsTab();

private:
	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	TSharedRef<Trace::IAnalysisService> AnalysisService;
	TSharedRef<Trace::ISessionService> SessionService;
	TSharedRef<Trace::IModuleService> ModuleService;

	/** The trace analysis session. */
	TSharedPtr<const Trace::IAnalysisSession> Session;

	/** The session handle. */
	Trace::FSessionHandle CurrentSessionHandle;

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
