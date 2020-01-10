// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/LoadingProfiler/LoadingProfilerCommands.h"

class SLoadingProfilerWindow;

namespace Trace
{
	class ISessionService;
	class IAnalysisService;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Asset Loading Profiler state and settings.
 */
class FLoadingProfilerManager : public TSharedFromThis<FLoadingProfilerManager>
{
	friend class FLoadingProfilerActionManager;

public:
	/** Creates the Loading Profiler (Asset Loading Insights) manager, only one instance can exist. */
	FLoadingProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FLoadingProfilerManager();

	/** Creates an instance of the profiler manager. */
	static TSharedPtr<FLoadingProfilerManager> Initialize()
	{
		if (FLoadingProfilerManager::Instance.IsValid())
		{
			FLoadingProfilerManager::Instance.Reset();
		}

		FLoadingProfilerManager::Instance = MakeShareable(new FLoadingProfilerManager(FInsightsManager::Get()->GetCommandList()));
		FLoadingProfilerManager::Instance->PostConstructor();

		return FLoadingProfilerManager::Instance;
	}

	/** Shutdowns the profiler manager. */
	void Shutdown()
	{
		FLoadingProfilerManager::Instance.Reset();
	}

protected:
	/** Finishes initialization of the profiler manager. */
	void PostConstructor();

	/** Binds our UI commands to delegates. */
	void BindCommands();

public:
	/**
	 * @return the global instance of the Loading Profiler (Asset Loading Insights) manager.
	 * This is an internal singleton and cannot be used outside ProfilerModule.
	 * For external use:
	 *     IProfilerModule& ProfilerModule = FModuleManager::Get().LoadModuleChecked<IProfilerModule>("Profiler");
	 *     ProfilerModule.GetProfilerManager();
	 */
	static TSharedPtr<FLoadingProfilerManager> Get();

	/** @returns UI command list for the Loading Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Loading Profiler commands. */
	static const FLoadingProfilerCommands& GetCommands();

	/** @return an instance of the Loading Profiler action manager. */
	static FLoadingProfilerActionManager& GetActionManager();

	void AssignProfilerWindow(const TSharedRef<SLoadingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindow = InProfilerWindow;
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SLoadingProfilerWindow> GetProfilerWindow() const
	{
		return ProfilerWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if Timing view is visible */
	const bool IsTimingViewVisible() const { return bIsTimingViewVisible; }
	void SetTimingViewVisible(const bool bIsVisible) { bIsTimingViewVisible = bIsVisible; }
	void ShowHideTimingView(const bool bIsVisible);

	/** @return true, if Event Aggregation tree view is visible */
	const bool IsEventAggregationTreeViewVisible() const { return bIsEventAggregationTreeViewVisible; }
	void SetEventAggregationTreeViewVisible(const bool bIsVisible) { bIsEventAggregationTreeViewVisible = bIsVisible; }
	void ShowHideEventAggregationTreeView(const bool bIsVisible);

	/** @return true, if Object Type Aggregation tree view is visible */
	const bool IsObjectTypeAggregationTreeViewVisible() const { return bIsObjectTypeAggregationTreeViewVisible; }
	void SetObjectTypeAggregationTreeViewVisible(const bool bIsVisible) { bIsObjectTypeAggregationTreeViewVisible = bIsVisible; }
	void ShowHideObjectTypeAggregationTreeView(const bool bIsVisible);

	/** @return true, if Package Details tree view is visible */
	const bool IsPackageDetailsTreeViewVisible() const { return bIsPackageDetailsTreeViewVisible; }
	void SetPackageDetailsTreeViewVisible(const bool bIsVisible) { bIsPackageDetailsTreeViewVisible = bIsVisible; }
	void ShowHidePackageDetailsTreeView(const bool bIsVisible);

	/** @return true, if Export Details tree view is visible */
	const bool IsExportDetailsTreeViewVisible() const { return bIsExportDetailsTreeViewVisible; }
	void SetExportDetailsTreeViewVisible(const bool bIsVisible) { bIsExportDetailsTreeViewVisible = bIsVisible; }
	void ShowHideExportDetailsTreeView(const bool bIsVisible);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void OnSessionChanged();

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

	/** An instance of the Loading Profiler action manager. */
	FLoadingProfilerActionManager ActionManager;

	/** A weak pointer to the Loading Profiler window. */
	TWeakPtr<class SLoadingProfilerWindow> ProfilerWindow;

	/** If the Timing view is visible or hidden. */
	bool bIsTimingViewVisible;

	/** If the Event Aggregation tree view is visible or hidden. */
	bool bIsEventAggregationTreeViewVisible;

	/** If the Object Type Aggregation tree view is visible or hidden. */
	bool bIsObjectTypeAggregationTreeViewVisible;

	/** If the Package Details tree view is visible or hidden. */
	bool bIsPackageDetailsTreeViewVisible;

	/** If the Export Details tree view is visible or hidden. */
	bool bIsExportDetailsTreeViewVisible;

	/** A shared pointer to the global instance of the profiler manager. */
	static TSharedPtr<FLoadingProfilerManager> Instance;
};
