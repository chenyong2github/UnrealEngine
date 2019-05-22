// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsSettings.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class STimingProfilerWindow;

namespace Trace
{
	class IAnalysisService;
	class IAnalysisSession;
	class ISessionService;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// Enumerates loading a trace file progress states.

enum class ELoadingProgressState
{
	Started,
	InProgress,
	Loaded,
	Failed,
	Cancelled,

	/// Invalid enum type, may be used as a number of enumerations.
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EInsightsNotificationType
{
	LoadingTraceFile,

	/// Invalid enum type, may be used as a number of enumerations.
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/// This class manages following areas:
///     Connecting/disconnecting to source trace
///     Lifetime of all other managers (specific profilers)
///     Global Unreal Insights application state and settings

class FInsightsManager
	: public TSharedFromThis<FInsightsManager>
{
	friend class FInsightsActionManager;

public:
	/// Creates the main manager, only one instance can exist.
	FInsightsManager(TSharedRef<Trace::IAnalysisService> TraceAnalysisService, TSharedRef<Trace::ISessionService> SessionService);

	/// Virtual destructor.
	virtual ~FInsightsManager();

	/// Creates an instance of the main manager and initializes global instance with the previously created instance of the manager.
	/// @param TraceAnalysisService The trace analysis service
	/// @param TraceSessionService  The trace session service
	static TSharedPtr<FInsightsManager> Initialize(TSharedRef<Trace::IAnalysisService> TraceAnalysisService, TSharedRef<Trace::ISessionService> TraceSessionService)
	{
		if (FInsightsManager::Instance.IsValid())
		{
			FInsightsManager::Instance.Reset();
		}

		FInsightsManager::Instance = MakeShareable(new FInsightsManager(TraceAnalysisService, TraceSessionService));
		FInsightsManager::Instance->PostConstructor();

		return FInsightsManager::Instance;
	}

	/// Shutdowns the main manager.
	void Shutdown()
	{
		FInsightsManager::Instance.Reset();
	}

protected:
	/// Finishes initialization of the profiler manager
	void PostConstructor();

	/// Binds our UI commands to delegates.
	void BindCommands();

public:
	/// @return the global instance of the main manager (FInsightsManager).
	static TSharedPtr<FInsightsManager> Get();

	/// @return an instance of the trace analysis session.
	TSharedPtr<const Trace::IAnalysisSession> GetSession() const;

	/// @returns UI command list for the main manager.
	const TSharedRef<FUICommandList> GetCommandList() const;

	/// @return an instance of the main commands.
	static const FInsightsCommands& GetCommands();

	/// @return an instance of the main action manager.
	static FInsightsActionManager& GetActionManager();

	/// @return an instance of the main settings.
	static FInsightsSettings& GetSettings();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/// @return true, if UI is allowed to display debug info
	const bool IsDebugInfoEnabled() const { return bIsDebugInfoEnabled; }
	void SetDebugInfo(const bool bDebugInfoEnabledState) { bIsDebugInfoEnabled = bDebugInfoEnabledState; }

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// SessionChangedEvent

public:
	/// The event to execute when the session has changed.
	DECLARE_EVENT(FTimingProfilerManager, FSessionChangedEvent);
	FSessionChangedEvent& GetSessionChangedEvent() { return SessionChangedEvent; }
protected:
	/// The event to execute when the session has changed.
	FSessionChangedEvent SessionChangedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

public:
	/// Creates a new profiler session instance initialized with mock data (for debugging purposes).
	void CreateMockSession();

	/// Creates a new profiler session instance and loads a live trace.
	void CreateLiveSession();

	/// Creates a new profiler session instance and loads a trace file from the specified location.
	/// @param TraceFilepath - The path to the trace file
	void LoadTraceFile(const FString& TraceFilepath);

	/// Opens the Settings dialog.
	void OpenSettings();

protected:
	/// Updates this manager, done through FCoreTicker.
	bool Tick(float DeltaTime);

	/// Resets (closes) current session instance.
	void ResetSession();

protected:
	/// The delegate to be invoked when this manager ticks.
	FTickerDelegate OnTick;

	/// Handle to the registered OnTick.
	FDelegateHandle OnTickHandle;

	TSharedRef<Trace::IAnalysisService> AnalysisService;
	TSharedRef<Trace::ISessionService> SessionService;

	/// The trace analysis session.
	TSharedPtr<const Trace::IAnalysisSession> Session;

	/// List of UI commands for this manager. This will be filled by this and corresponding classes.
	TSharedRef<FUICommandList> CommandList;

	/// An instance of the main action manager.
	FInsightsActionManager ActionManager;

	/// An instance of the main settings.
	FInsightsSettings Settings;

	/// If enabled, UI can display additional info for debugging purposes.
	bool bIsDebugInfoEnabled;

	/// A shared pointer to the global instance of the main manager.
	static TSharedPtr<FInsightsManager> Instance;
};
