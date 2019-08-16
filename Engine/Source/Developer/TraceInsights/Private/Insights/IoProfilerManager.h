// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IoProfilerCommands.h"

class SIoProfilerWindow;

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
class FIoProfilerManager : public TSharedFromThis<FIoProfilerManager>
{
	friend class FIoProfilerActionManager;

public:
	/** Creates the Timing Profiler manager, only one instance can exist. */
	FIoProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FIoProfilerManager();

	/** Creates an instance of the profiler manager. */
	static TSharedPtr<FIoProfilerManager> Initialize()
	{
		if (FIoProfilerManager::Instance.IsValid())
		{
			FIoProfilerManager::Instance.Reset();
		}

		FIoProfilerManager::Instance = MakeShareable(new FIoProfilerManager(FInsightsManager::Get()->GetCommandList()));
		FIoProfilerManager::Instance->PostConstructor();

		return FIoProfilerManager::Instance;
	}

	/** Shutdowns the Timing Profiler manager. */
	void Shutdown()
	{
		FIoProfilerManager::Instance.Reset();
	}

protected:
	/** Finishes initialization of the profiler manager. */
	void PostConstructor();

	/** Binds our UI commands to delegates. */
	void BindCommands();

public:
	/**
	 * @return the global instance of the Timing Profiler manager (FIoProfilerManager).
	 * This is an internal singleton and cannot be used outside ProfilerModule.
	 * For external use:
	 *     IProfilerModule& ProfilerModule = FModuleManager::Get().LoadModuleChecked<IProfilerModule>("Profiler");
	 *     ProfilerModule.GetProfilerManager();
	 */
	static TSharedPtr<FIoProfilerManager> Get();

	/** @returns UI command list for the Timing Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Timing Profiler commands. */
	static const FIoProfilerCommands& GetCommands();

	/** @return an instance of the Timing Profiler action manager. */
	static FIoProfilerActionManager& GetActionManager();

	void AssignProfilerWindow(const TSharedRef<SIoProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindow = InProfilerWindow;
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SIoProfilerWindow> GetProfilerWindow() const
	{
		return ProfilerWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if Timing View is visible */
	const bool IsTimingViewVisible() const { return bIsTimingViewVisible; }
	void SetTimingViewVisible(const bool bTimingViewVisibleState);

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

	/** An instance of the Timing Manager action manager. */
	FIoProfilerActionManager ActionManager;

	/** A weak pointer to the Timing Profiler window. */
	TWeakPtr<class SIoProfilerWindow> ProfilerWindow;

	/** If the Timing View is visible or hidden. */
	bool bIsTimingViewVisible;

	/** A shared pointer to the global instance of the profiler manager. */
	static TSharedPtr<FIoProfilerManager> Instance;
};
