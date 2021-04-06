// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IUnrealInsightsModule.h"

namespace Insights
{

class STaskTableTreeView;

struct FTaskGraphProfilerTabs
{
	// Tab identifiers
	static const FName TaskTableTreeViewTabID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Task Graph Profiler state and settings.
 */
class FTaskGraphProfilerManager : public TSharedFromThis<FTaskGraphProfilerManager>, public IInsightsComponent
{
public:
	/** Creates the Memory Profiler manager, only one instance can exist. */
	FTaskGraphProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FTaskGraphProfilerManager();

	/** Creates an instance of the Memory Profiler manager. */
	static TSharedPtr<FTaskGraphProfilerManager> CreateInstance();

	/**
	 * @return the global instance of the Task Graph Profiler manager.
	 * This is an internal singleton and cannot be used outside TraceInsights.
	 * For external use:
	 *     IUnrealInsightsModule& Module = FModuleManager::Get().LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	 *     Module.GetTaskGraphProfilerManager();
	 */
	static TSharedPtr<FTaskGraphProfilerManager> Get();

	//////////////////////////////////////////////////
	// IInsightsComponent

	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;
	virtual void OnWindowClosedEvent() override {}

	TSharedRef<SDockTab> SpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args);
	void OnTaskTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	//////////////////////////////////////////////////
	bool GetIsAvailable() { return bIsAvailable; }

	void OnSessionChanged();

private:
	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

	void RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender);

private:
	bool bIsInitialized;
	bool bIsAvailable;
	FAvailabilityCheck AvailabilityCheck;

	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	/** A shared pointer to the global instance of the Task Graph Profiler manager. */
	static TSharedPtr<FTaskGraphProfilerManager> Instance;

	TWeakPtr<FTabManager> TimingTabManager;

	TSharedPtr<Insights::STaskTableTreeView> TaskTableTreeView;
};

} // namespace Insights

