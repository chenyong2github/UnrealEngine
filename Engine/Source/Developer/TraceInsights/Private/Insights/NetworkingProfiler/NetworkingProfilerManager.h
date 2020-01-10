// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerCommands.h"

class SNetworkingProfilerWindow;

namespace Trace
{
	class ISessionService;
	class IAnalysisService;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Networking Profiler state and settings.
 */
class FNetworkingProfilerManager : public TSharedFromThis<FNetworkingProfilerManager>
{
	friend class FNetworkingProfilerActionManager;

public:
	/** Creates the Networking Profiler (Networking Insights) manager, only one instance can exist. */
	FNetworkingProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FNetworkingProfilerManager();

	/** Creates an instance of the profiler manager. */
	static TSharedPtr<FNetworkingProfilerManager> Initialize()
	{
		if (FNetworkingProfilerManager::Instance.IsValid())
		{
			FNetworkingProfilerManager::Instance.Reset();
		}

		FNetworkingProfilerManager::Instance = MakeShareable(new FNetworkingProfilerManager(FInsightsManager::Get()->GetCommandList()));
		FNetworkingProfilerManager::Instance->PostConstructor();

		return FNetworkingProfilerManager::Instance;
	}

	/** Shutdowns the Networking Profiler manager. */
	void Shutdown()
	{
		FNetworkingProfilerManager::Instance.Reset();
	}

protected:
	/** Finishes initialization of the profiler manager. */
	void PostConstructor();

	/** Binds our UI commands to delegates. */
	void BindCommands();

public:
	/**
	 * @return the global instance of the Networking Profiler (Networking Insights) manager.
	 * This is an internal singleton and cannot be used outside ProfilerModule.
	 * For external use:
	 *     IProfilerModule& ProfilerModule = FModuleManager::Get().LoadModuleChecked<IProfilerModule>("Profiler");
	 *     ProfilerModule.GetProfilerManager();
	 */
	static TSharedPtr<FNetworkingProfilerManager> Get();

	/** @returns UI command list for the Networking Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Networking Profiler commands. */
	static const FNetworkingProfilerCommands& GetCommands();

	/** @return an instance of the Networking Profiler action manager. */
	static FNetworkingProfilerActionManager& GetActionManager();

	void AddProfilerWindow(const TSharedRef<SNetworkingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindows.Add(InProfilerWindow);
	}

	void RemoveProfilerWindow(const TSharedRef<SNetworkingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindows.Remove(InProfilerWindow);
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SNetworkingProfilerWindow> GetProfilerWindow(int32 Index) const
	{
		return ProfilerWindows[Index].Pin();
	}

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

	/** An instance of the Networking Profiler action manager. */
	FNetworkingProfilerActionManager ActionManager;

	/** A list of weak pointers to the Networking Profiler windows. */
	TArray<TWeakPtr<class SNetworkingProfilerWindow>> ProfilerWindows;

	/** A shared pointer to the global instance of the profiler manager. */
	static TSharedPtr<FNetworkingProfilerManager> Instance;
};
