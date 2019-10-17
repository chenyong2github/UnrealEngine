// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommands.h"
#include "Insights/ViewModels/TimerNode.h"

class STimingProfilerWindow;

namespace Trace
{
	class ISessionService;
	class IAnalysisService;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Timing Profiler state and settings.
 */
class FTimingProfilerManager : public TSharedFromThis<FTimingProfilerManager>
{
	friend class FTimingProfilerActionManager;

public:
	/** Creates the Timing Profiler manager, only one instance can exist. */
	FTimingProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FTimingProfilerManager();

	/** Creates an instance of the profiler manager. */
	static TSharedPtr<FTimingProfilerManager> Initialize()
	{
		if (FTimingProfilerManager::Instance.IsValid())
		{
			FTimingProfilerManager::Instance.Reset();
		}

		FTimingProfilerManager::Instance = MakeShareable(new FTimingProfilerManager(FInsightsManager::Get()->GetCommandList()));
		FTimingProfilerManager::Instance->PostConstructor();

		return FTimingProfilerManager::Instance;
	}

	/** Shutdowns the Timing Profiler manager. */
	void Shutdown()
	{
		FTimingProfilerManager::Instance.Reset();
	}

protected:
	/** Finishes initialization of the profiler manager. */
	void PostConstructor();

	/** Binds our UI commands to delegates. */
	void BindCommands();

public:
	/**
	 * @return the global instance of the Timing Profiler (Timing Insights) manager.
	 * This is an internal singleton and cannot be used outside ProfilerModule.
	 * For external use:
	 *     IProfilerModule& ProfilerModule = FModuleManager::Get().LoadModuleChecked<IProfilerModule>("Profiler");
	 *     ProfilerModule.GetProfilerManager();
	 */
	static TSharedPtr<FTimingProfilerManager> Get();

	/** @returns UI command list for the Timing Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Timing Profiler commands. */
	static const FTimingProfilerCommands& GetCommands();

	/** @return an instance of the Timing Profiler action manager. */
	static FTimingProfilerActionManager& GetActionManager();

	void AssignProfilerWindow(const TSharedRef<STimingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindow = InProfilerWindow;
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class STimingProfilerWindow> GetProfilerWindow() const
	{
		return ProfilerWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if Frames Track is visible */
	const bool IsFramesTrackVisible() const { return bIsFramesTrackVisible; }
	void SetFramesTrackVisible(const bool bIsVisible) { bIsFramesTrackVisible = bIsVisible; }
	void ShowHideFramesTrack(const bool bIsVisible);

	/** @return true, if Graph Track is visible */
	const bool IsGraphTrackVisible() const { return bIsGraphTrackVisible; }
	void SetGraphTrackVisible(const bool bIsVisible) { bIsGraphTrackVisible = bIsVisible; }
	void ShowHideGraphTrack(const bool bIsVisible);

	/** @return true, if Timing View is visible */
	const bool IsTimingViewVisible() const { return bIsTimingViewVisible; }
	void SetTimingViewVisible(const bool bIsVisible) { bIsTimingViewVisible = bIsVisible; }
	void ShowHideTimingView(const bool bIsVisible);

	/** @return true, if Timers View is visible */
	const bool IsTimersViewVisible() const { return bIsTimersViewVisible; }
	void SetTimersViewVisible(const bool bIsVisible) { bIsTimersViewVisible = bIsVisible; }
	void ShowHideTimersView(const bool bIsVisible);

	/** @return true, if Callers Tree View is visible */
	const bool IsCallersTreeViewVisible() const { return bIsCallersTreeViewVisible; }
	void SetCallersTreeViewVisible(const bool bIsVisible) { bIsCallersTreeViewVisible = bIsVisible; }
	void ShowHideCallersTreeView(const bool bIsVisible);

	/** @return true, if Callees Tree View is visible */
	const bool IsCalleesTreeViewVisible() const { return bIsCalleesTreeViewVisible; }
	void SetCalleesTreeViewVisible(const bool bIsVisible) { bIsCalleesTreeViewVisible = bIsVisible; }
	void ShowHideCalleesTreeView(const bool bIsVisible);

	/** @return true, if Stats Counters View is visible */
	const bool IsStatsCountersViewVisible() const { return bIsStatsCountersViewVisible; }
	void SetStatsCountersViewVisible(const bool bIsVisible) { bIsStatsCountersViewVisible = bIsVisible; }
	void ShowHideStatsCountersView(const bool bIsVisible);

	/** @return true, if Log View is visible */
	const bool IsLogViewVisible() const { return bIsLogViewVisible; }
	void SetLogViewVisible(const bool bIsVisible) { bIsLogViewVisible = bIsVisible; }
	void ShowHideLogView(const bool bIsVisible);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void OnSessionChanged();

	const FTimerNodePtr GetTimerNode(uint64 TypeId) const;

	void SetSelectedTimeRange(const double InStartTime, const double InEndTime);
	void SetSelectedTimer(const uint64 InTimerId);

	void OnThreadFilterChanged();

	void ResetCallersAndCallees();
	void UpdateCallersAndCallees();

	void UpdateAggregatedTimerStats();
	void UpdateAggregatedCounterStats();

protected:
	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

protected:
	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	/** List of UI commands for this manager. This will be filled by this and corresponding classes. */
	TSharedRef<FUICommandList> CommandList;

	/** An instance of the Timing Profiler action manager. */
	FTimingProfilerActionManager ActionManager;

	/** A weak pointer to the Timing Profiler window. */
	TWeakPtr<class STimingProfilerWindow> ProfilerWindow;

	/** If the Frames Track is visible or hidden. */
	bool bIsFramesTrackVisible;

	/** If the Graph Track is visible or hidden. */
	bool bIsGraphTrackVisible;

	/** If the Timing View is visible or hidden. */
	bool bIsTimingViewVisible;

	/** If the Timers View is visible or hidden. */
	bool bIsTimersViewVisible;

	/** If the Callers Tree View is visible or hidden. */
	bool bIsCallersTreeViewVisible;

	/** If the Callees Tree View is visible or hidden. */
	bool bIsCalleesTreeViewVisible;

	/** If the Stats Counters View is visible or hidden. */
	bool bIsStatsCountersViewVisible;

	/** If the Log View is visible or hidden. */
	bool bIsLogViewVisible;

	/** A shared pointer to the global instance of the profiler manager. */
	static TSharedPtr<FTimingProfilerManager> Instance;

	//////////////////////////////////////////////////

	double SelectionStartTime;
	double SelectionEndTime;

	static const uint64 InvalidTimerId = uint64(-1);
	uint64 SelectedTimerId;
};
