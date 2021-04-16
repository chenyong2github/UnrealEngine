// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskGraphProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TasksProfiler.h"
#include "Async/TaskGraphInterfaces.h"

#include "Insights/InsightsStyle.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTable.h"
#include "Insights/TaskGraphProfiler/Widgets/STaskTableTreeView.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "TaskGraphProfilerManager"

namespace Insights
{

const FName FTaskGraphProfilerTabs::TaskTableTreeViewTabID(TEXT("TaskTableTreeView"));

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::Get()
{
	return FTaskGraphProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::CreateInstance()
{
	ensure(!FTaskGraphProfilerManager::Instance.IsValid());
	if (FTaskGraphProfilerManager::Instance.IsValid())
	{
		FTaskGraphProfilerManager::Instance.Reset();
	}

	FTaskGraphProfilerManager::Instance = MakeShared<FTaskGraphProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FTaskGraphProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskGraphProfilerManager::FTaskGraphProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FTaskGraphProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 0.0f);

	FOnRegisterMajorTabExtensions* TimingProfilerLayoutExtension = InsightsModule.FindMajorTabLayoutExtension(FInsightsManagerTabs::TimingProfilerTabId);
	if (TimingProfilerLayoutExtension)
	{
		TimingProfilerLayoutExtension->AddRaw(this, &FTaskGraphProfilerManager::RegisterTimingProfilerLayoutExtensions);
	}

	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &FTaskGraphProfilerManager::OnSessionChanged);
	OnSessionChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FTaskGraphProfilerManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskGraphProfilerManager::~FTaskGraphProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::UnregisterMajorTabs()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTaskGraphProfilerManager::Tick(float DeltaTime)
{
	// Check if session has Memory events (to spawn the tab), but not too often.
	if (!bIsAvailable && AvailabilityCheck.Tick())
	{
		bool bShouldBeAvailable = false;

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());
			TSharedPtr<FTabManager> TabManagerShared = TimingTabManager.Pin();
			if (TasksProvider && TasksProvider->GetNumTasks() > 0 && TabManagerShared.IsValid())
			{
				TabManagerShared->TryInvokeTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID);
				bIsAvailable = true;
			}

			if (Session->IsAnalysisComplete())
			{
				// Never check again during this session.
				AvailabilityCheck.Disable();
			}
		}
		else
		{
			// Do not check again until the next session changed event (see OnSessionChanged).
			AvailabilityCheck.Disable();
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnSessionChanged()
{
	bIsAvailable = false;
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		AvailabilityCheck.Enable(0.5);
	}
	else
	{
		AvailabilityCheck.Disable();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender)
{
	TimingTabManager = InOutExtender.GetTabManager();

	FInsightsMinorTabConfig& MinorTabConfig = InOutExtender.AddMinorTabConfig();
	MinorTabConfig.TabId = FTaskGraphProfilerTabs::TaskTableTreeViewTabID;
	MinorTabConfig.TabLabel = LOCTEXT("TaskTableTreeViewTabTitle", "Tasks");
	MinorTabConfig.TabTooltip = LOCTEXT("TaskTableTreeViewTabTitleTooltip", "Opens the Task Table Tree View tab, that allows Task Graph profilling.");
	MinorTabConfig.TabIcon = FSlateIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small"));
	MinorTabConfig.OnSpawnTab = FOnSpawnTab::CreateRaw(this, &FTaskGraphProfilerManager::SpawnTab_TaskTableTreeView);

	InOutExtender.GetLayoutExtender().ExtendLayout(FTimingProfilerTabs::StatsCountersID
		, ELayoutExtensionPosition::After
		, FTabManager::FTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID, ETabState::ClosedTab));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTaskGraphProfilerManager::SpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args)
{
	TSharedRef<FTaskTable> TaskTable = MakeShared<FTaskTable>();
	TaskTable->Reset();

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TaskTableTreeView, STaskTableTreeView, TaskTable)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTaskGraphProfilerManager::OnTaskTableTreeViewTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnTaskTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	TaskTableTreeView.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::GetTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, AddRelationCallback Callback)
{
	const int32 MaxTasksToShow = 30;

	if (Task == nullptr)
	{
		return;
	}

	if (Task->CreatedTimestamp != Task->LaunchedTimestamp || Task->CreatedThreadId != Task->LaunchedThreadId)
	{
		Callback(Task->CreatedTimestamp, Task->CreatedThreadId, Task->LaunchedTimestamp, Task->LaunchedThreadId, FTaskGraphRelation::ETaskGraphRelationType::Created);
	}

	Callback(Task->LaunchedTimestamp, Task->LaunchedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, FTaskGraphRelation::ETaskGraphRelationType::Launched);

	int32 NumPrerequisitesToShow = FMath::Min(Task->Prerequisites.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumPrerequisitesToShow; ++i)
	{
		const TraceServices::FTaskInfo* Prerequisite = TasksProvider->TryGetTask(Task->Prerequisites[i].RelativeId);
		check(Prerequisite != nullptr);
		Callback(Prerequisite->CompletedTimestamp, Prerequisite->CompletedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, FTaskGraphRelation::ETaskGraphRelationType::Prerequisite);
	}

	if (Task->LaunchedTimestamp != Task->ScheduledTimestamp || Task->LaunchedThreadId != Task->ScheduledThreadId)
	{
		Callback(Task->ScheduledTimestamp, Task->ScheduledThreadId, Task->StartedTimestamp, Task->StartedThreadId, FTaskGraphRelation::ETaskGraphRelationType::Scheduled);
	}

	int32 NumNestedToShow = FMath::Min(Task->NestedTasks.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumNestedToShow; ++i)
	{
		const TraceServices::FTaskInfo::FRelationInfo& RelationInfo = Task->NestedTasks[i];
		const TraceServices::FTaskInfo* NestedTask = TasksProvider->TryGetTask(RelationInfo.RelativeId);
		check(NestedTask != nullptr);

		Callback(RelationInfo.Timestamp, Task->StartedThreadId, NestedTask->StartedTimestamp, NestedTask->StartedThreadId, FTaskGraphRelation::ETaskGraphRelationType::AddedNested);

		Callback(NestedTask->CompletedTimestamp, NestedTask->CompletedThreadId, NestedTask->CompletedTimestamp, Task->StartedThreadId, FTaskGraphRelation::ETaskGraphRelationType::NestedCompleted);
	}

	int32 NumSubsequentsToShow = FMath::Min(Task->NestedTasks.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumSubsequentsToShow; ++i)
	{
		const TraceServices::FTaskInfo* Subsequent = TasksProvider->TryGetTask(Task->Subsequents[i].RelativeId);
		check(Subsequent != nullptr);
		Callback(Task->CompletedTimestamp, Task->CompletedThreadId, Subsequent->ScheduledTimestamp, Subsequent->ScheduledThreadId, FTaskGraphRelation::ETaskGraphRelationType::Subsequent);
	}

	if (Task->FinishedTimestamp != Task->CompletedTimestamp || Task->CompletedThreadId != Task->StartedThreadId)
	{
		Callback(Task->FinishedTimestamp, Task->StartedThreadId, Task->CompletedTimestamp, Task->StartedThreadId, FTaskGraphRelation::ETaskGraphRelationType::Completed);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::GetTaskRelations(double Time, uint32 ThreadId, AddRelationCallback Callback)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(ThreadId, Time);

	if (Task != nullptr)
	{
		GetTaskRelations(Task, TasksProvider, Callback);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::GetTaskRelations(uint32 TaskId, AddRelationCallback Callback)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(TaskId);

	if (Task != nullptr)
	{
		GetTaskRelations(Task, TasksProvider, Callback);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
