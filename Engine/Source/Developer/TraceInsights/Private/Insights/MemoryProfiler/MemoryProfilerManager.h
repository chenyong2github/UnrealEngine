// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "Logging/LogMacros.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/MemoryProfiler/MemoryProfilerCommands.h"

class FMemorySharedState;
class SMemoryProfilerWindow;

DECLARE_LOG_CATEGORY_EXTERN(MemoryProfiler, Log, All);

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Memory Profiler (Asset Memory Insights) state and settings.
 */
class FMemoryProfilerManager : public TSharedFromThis<FMemoryProfilerManager>, public IInsightsComponent
{
	friend class FMemoryProfilerActionManager;

public:
	/** Creates the Memory Profiler manager, only one instance can exist. */
	FMemoryProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FMemoryProfilerManager();

	/** Creates an instance of the Memory Profiler manager. */
	static TSharedPtr<FMemoryProfilerManager> CreateInstance();

	/**
	 * @return the global instance of the Memory Profiler manager.
	 * This is an internal singleton and cannot be used outside TraceInsights.
	 * For external use:
	 *     IUnrealInsightsModule& Module = FModuleManager::Get().LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	 *     Module.GetMemoryProfilerManager();
	 */
	static TSharedPtr<FMemoryProfilerManager> Get();

	//////////////////////////////////////////////////
	// IInsightsComponent

	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;

	//////////////////////////////////////////////////

	/** @returns UI command list for the Memory Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Memory Profiler commands. */
	static const FMemoryProfilerCommands& GetCommands();

	/** @return an instance of the Memory Profiler action manager. */
	static FMemoryProfilerActionManager& GetActionManager();

	void AssignProfilerWindow(const TSharedRef<SMemoryProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindow = InProfilerWindow;
	}

	void RemoveProfilerWindow()
	{
		ProfilerWindow.Reset();
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SMemoryProfilerWindow> GetProfilerWindow() const
	{
		return ProfilerWindow.Pin();
	}

	FMemorySharedState* GetSharedState();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if the Timing view is visible */
	const bool IsTimingViewVisible() const { return bIsTimingViewVisible; }
	void SetTimingViewVisible(const bool bIsVisible) { bIsTimingViewVisible = bIsVisible; }
	void ShowHideTimingView(const bool bIsVisible);

	/** @return true, if the Export Details tree view is visible */
	const bool IsMemTagTreeViewVisible() const { return bIsMemTagTreeViewVisible; }
	void SetMemTagTreeViewVisible(const bool bIsVisible) { bIsMemTagTreeViewVisible = bIsVisible; }
	void ShowHideMemTagTreeView(const bool bIsVisible);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void OnSessionChanged();

private:
	/** Binds our UI commands to delegates. */
	void BindCommands();

	/** Called to spawn the Memory Profiler major tab. */
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	bool CanSpawnTab(const FSpawnTabArgs& Args) const;

	/** Callback called when the Memory Profiler major tab is closed. */
	void OnTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

private:
	bool bIsInitialized;
	bool bIsAvailable;
	FAvailabilityCheck AvailabilityCheck;

	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	/** List of UI commands for this manager. This will be filled by this and corresponding classes. */
	TSharedRef<FUICommandList> CommandList;

	/** An instance of the Memory Profiler action manager. */
	FMemoryProfilerActionManager ActionManager;

	/** A weak pointer to the Memory Profiler window. */
	TWeakPtr<class SMemoryProfilerWindow> ProfilerWindow;

	/** If the Timing view is visible or hidden. */
	bool bIsTimingViewVisible;

	/** If the Categories tree view is visible or hidden. */
	bool bIsMemTagTreeViewVisible;

	/** A shared pointer to the global instance of the Memory Profiler manager. */
	static TSharedPtr<FMemoryProfilerManager> Instance;
};
