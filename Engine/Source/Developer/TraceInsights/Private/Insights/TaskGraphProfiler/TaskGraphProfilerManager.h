// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IUnrealInsightsModule.h"

namespace TraceServices
{
	struct FTaskInfo;
	class ITasksProvider;
}

class FThreadTrackEvent;

namespace Insights
{

class STaskTableTreeView;
class FTaskTimingSharedState;

struct FTaskGraphProfilerTabs
{
	// Tab identifiers
	static const FName TaskTableTreeViewTabID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETaskEventType : uint32
{
	Created,
	Launched,
	Prerequisite,
	Scheduled,
	Started,
	AddedNested,
	NestedCompleted,
	Subsequent,
	Completed,
	
	NumTaskEventTypes,
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Task Graph Profiler state and settings.
 */
class FTaskGraphProfilerManager : public TSharedFromThis<FTaskGraphProfilerManager>, public IInsightsComponent
{
public:
	typedef TFunctionRef<void(double /*SourceTimestamp*/, uint32 /*SourceThreadId*/, double /*TargetTimestamp*/, uint32 /*TargetThreadId*/, ETaskEventType /*Type*/)> FAddRelationCallback;

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
	virtual void OnWindowClosedEvent() override;

	TSharedRef<SDockTab> SpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args);
	bool CanSpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args);
	void OnTaskTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	//////////////////////////////////////////////////
	bool GetIsAvailable() { return bIsAvailable; }

	void OnSessionChanged();

	void ShowTaskRelations(const FThreadTrackEvent* InSelectedEvent, uint32 ThreadId);
	void ShowTaskRelations(uint32 TaskId);
	void AddRelation(const FThreadTrackEvent* InSelectedEvent, double SourceTimestamp, uint32 SourceThreadId, double TargetTimestamp, uint32 TargetThreadId, ETaskEventType Type);
	void ClearTaskRelations();
	FLinearColor GetColorForTaskEvent(ETaskEventType InEvent);

	TSharedPtr<Insights::FTaskTimingSharedState> GetTaskTimingSharedState() { return TaskTimingSharedState;	}

	bool GetShowRelations() const { return bShowRelations; }
	void SetShowRelations(bool bInValue);

private:
	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

	void RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender);

	void ShowTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, const FThreadTrackEvent* InSelectedEvent);
	void GetSingleTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, const FThreadTrackEvent* InSelectedEvent);

	void InitializeColorCode();

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

	/** Shared state for task tracks */
	TSharedPtr<FTaskTimingSharedState> TaskTimingSharedState;

	TWeakPtr<FTabManager> TimingTabManager;

	TSharedPtr<Insights::STaskTableTreeView> TaskTableTreeView;
	FLinearColor ColorCode[static_cast<uint32>(ETaskEventType::NumTaskEventTypes)];
	bool bShowRelations = true;
};

} // namespace Insights

