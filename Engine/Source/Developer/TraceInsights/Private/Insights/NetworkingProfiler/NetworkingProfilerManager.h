// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
 * This class manages the Timing Profiler state and settings.
 */
class FNetworkingProfilerManager : public TSharedFromThis<FNetworkingProfilerManager>
{
	friend class FNetworkingProfilerActionManager;

public:
	/** Creates the Timing Profiler manager, only one instance can exist. */
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

	/** Shutdowns the Timing Profiler manager. */
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
	 * @return the global instance of the Networking Profiler (Asset Networking Insights) manager.
	 * This is an internal singleton and cannot be used outside ProfilerModule.
	 * For external use:
	 *     IProfilerModule& ProfilerModule = FModuleManager::Get().LoadModuleChecked<IProfilerModule>("Profiler");
	 *     ProfilerModule.GetProfilerManager();
	 */
	static TSharedPtr<FNetworkingProfilerManager> Get();

	/** @returns UI command list for the Timing Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Timing Profiler commands. */
	static const FNetworkingProfilerCommands& GetCommands();

	/** @return an instance of the Timing Profiler action manager. */
	static FNetworkingProfilerActionManager& GetActionManager();

	void AssignProfilerWindow(const TSharedRef<SNetworkingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindow = InProfilerWindow;
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SNetworkingProfilerWindow> GetProfilerWindow() const
	{
		return ProfilerWindow.Pin();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Getters and setters used by Toggle Commands.

	/** @return true, if Packet Sizes view is visible */
	const bool IsPacketSizesViewVisible() const { return bIsPacketSizesViewVisible; }
	void SetPacketSizesViewVisible(const bool bPacketSizesViewVisibleState) { bIsPacketSizesViewVisible = bPacketSizesViewVisibleState; }
	void ShowHidePacketSizesView(const bool bPacketSizesViewVisibleState);

	/** @return true, if Packet Breakdown view is visible */
	const bool IsPacketBreakdownViewVisible() const { return bIsPacketBreakdownViewVisible; }
	void SetPacketBreakdownViewVisible(const bool bVisibleState) { bIsPacketBreakdownViewVisible = bVisibleState; }
	void ShowHidePacketBreakdownView(const bool bVisibleState);

	/** @return true, if Data Stream Breakdown view is visible */
	const bool IsDataStreamBreakdownViewVisible() const { return bIsDataStreamBreakdownViewVisible; }
	void SetDataStreamBreakdownViewVisible(const bool bVisibleState) { bIsDataStreamBreakdownViewVisible = bVisibleState; }
	void ShowHideDataStreamBreakdownView(const bool bVisibleState);

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

	/** A weak pointer to the Networking Profiler window. */
	TWeakPtr<class SNetworkingProfilerWindow> ProfilerWindow;

	/** If the Packet Sizes view is visible or hidden. */
	bool bIsPacketSizesViewVisible;

	/** If the Packet Breakdown view is visible or hidden. */
	bool bIsPacketBreakdownViewVisible;

	/** If the Data Stream Breakdown tree view is visible or hidden. */
	bool bIsDataStreamBreakdownViewVisible;

	/** A shared pointer to the global instance of the profiler manager. */
	static TSharedPtr<FNetworkingProfilerManager> Instance;
};
