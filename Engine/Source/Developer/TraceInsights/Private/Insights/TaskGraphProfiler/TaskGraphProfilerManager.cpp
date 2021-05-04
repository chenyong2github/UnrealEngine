// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskGraphProfilerManager.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TasksProfiler.h"
#include "Async/TaskGraphInterfaces.h"

#include "Insights/InsightsStyle.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTable.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTimingTrack.h"
#include "Insights/TaskGraphProfiler/Widgets/STaskTableTreeView.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TaskGraphRelation.h"
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

	InitializeColorCode();

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

	if (TaskTimingSharedState.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, TaskTimingSharedState.Get());
	}
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
				TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
				if (!Window.IsValid())
				{
					return true;
				}

				TSharedPtr<STimingView> TimingView = Window->GetTimingView();
				if (!TimingView.IsValid())
				{
					return true;
				}

				bIsAvailable = true;

				if (!TaskTimingSharedState.IsValid())
				{
					TaskTimingSharedState = MakeShared<FTaskTimingSharedState>(TimingView.Get());
					IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, TaskTimingSharedState.Get());
				}
				TabManagerShared->TryInvokeTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID);
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
	MinorTabConfig.CanSpawnTab = FCanSpawnTab::CreateRaw(this, &FTaskGraphProfilerManager::CanSpawnTab_TaskTableTreeView);

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

bool FTaskGraphProfilerManager::CanSpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args)
{
	return bIsAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnTaskTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	TaskTableTreeView.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::GetTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, FAddRelationCallback Callback)
{
	check(Task != nullptr);

	auto GetSingleTaskRelationsForAll = [this, TasksProvider, &Callback](const TArray< TraceServices::FTaskInfo::FRelationInfo>& Collection)
	{
		for (const TraceServices::FTaskInfo::FRelationInfo& Relation : Collection)
		{
			const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(Relation.RelativeId);
			if (Task != nullptr)
			{
				GetSingleTaskRelations(Task, TasksProvider, Callback);
			}
		}
	};


	GetSingleTaskRelationsForAll(Task->Prerequisites);
	GetSingleTaskRelations(Task, TasksProvider, Callback);
	GetSingleTaskRelationsForAll(Task->NestedTasks);
	GetSingleTaskRelationsForAll(Task->Subsequents);
}

void FTaskGraphProfilerManager::GetSingleTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, FAddRelationCallback Callback)
{
	const int32 MaxTasksToShow = 30;

	if (Task->CreatedTimestamp != Task->LaunchedTimestamp || Task->CreatedThreadId != Task->LaunchedThreadId)
	{
		Callback(Task->CreatedTimestamp, Task->CreatedThreadId, Task->LaunchedTimestamp, Task->LaunchedThreadId, ETaskEventType::Created);
	}

	Callback(Task->LaunchedTimestamp, Task->LaunchedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, ETaskEventType::Launched);

	int32 NumPrerequisitesToShow = FMath::Min(Task->Prerequisites.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumPrerequisitesToShow; ++i)
	{
		const TraceServices::FTaskInfo* Prerequisite = TasksProvider->TryGetTask(Task->Prerequisites[i].RelativeId);
		check(Prerequisite != nullptr);
		Callback(Prerequisite->CompletedTimestamp, Prerequisite->CompletedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, ETaskEventType::Prerequisite);
	}

	if (Task->LaunchedTimestamp != Task->ScheduledTimestamp || Task->LaunchedThreadId != Task->ScheduledThreadId)
	{
		Callback(Task->ScheduledTimestamp, Task->ScheduledThreadId, Task->StartedTimestamp, Task->StartedThreadId, ETaskEventType::Scheduled);
	}

	int32 NumNestedToShow = FMath::Min(Task->NestedTasks.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumNestedToShow; ++i)
	{
		const TraceServices::FTaskInfo::FRelationInfo& RelationInfo = Task->NestedTasks[i];
		const TraceServices::FTaskInfo* NestedTask = TasksProvider->TryGetTask(RelationInfo.RelativeId);
		check(NestedTask != nullptr);

		Callback(RelationInfo.Timestamp, Task->StartedThreadId, NestedTask->StartedTimestamp, NestedTask->StartedThreadId, ETaskEventType::AddedNested);

		Callback(NestedTask->CompletedTimestamp, NestedTask->CompletedThreadId, NestedTask->CompletedTimestamp, Task->StartedThreadId, ETaskEventType::NestedCompleted);
	}

	int32 NumSubsequentsToShow = FMath::Min(Task->Subsequents.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumSubsequentsToShow; ++i)
	{
		const TraceServices::FTaskInfo* Subsequent = TasksProvider->TryGetTask(Task->Subsequents[i].RelativeId);
		check(Subsequent != nullptr);
		if (Task->CompletedTimestamp < Subsequent->ScheduledTimestamp)
		{
			Callback(Task->CompletedTimestamp, Task->CompletedThreadId, Subsequent->ScheduledTimestamp, Subsequent->ScheduledThreadId, ETaskEventType::Subsequent);
		}
	}

	if (Task->FinishedTimestamp != Task->CompletedTimestamp || Task->CompletedThreadId != Task->StartedThreadId)
	{
		Callback(Task->FinishedTimestamp, Task->StartedThreadId, Task->CompletedTimestamp, Task->StartedThreadId, ETaskEventType::Completed);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::GetTaskRelations(double Time, uint32 ThreadId, FAddRelationCallback Callback)
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

void FTaskGraphProfilerManager::GetTaskRelations(uint32 TaskId, FAddRelationCallback Callback)
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

void FTaskGraphProfilerManager::OnWindowClosedEvent()
{
	TSharedPtr<FTabManager> TimingTabManagerSharedPtr = TimingTabManager.Pin();

	if (TimingTabManagerSharedPtr.IsValid())
	{
		TSharedPtr<SDockTab> Tab = TimingTabManagerSharedPtr->FindExistingLiveTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID);
		if (Tab.IsValid())
		{
			Tab->RequestCloseTab();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::InitializeColorCode()
{
	ColorCode[static_cast<uint32>(ETaskEventType::Created)] = FLinearColor::Yellow;
	ColorCode[static_cast<uint32>(ETaskEventType::Launched)] = FLinearColor::Green;
	ColorCode[static_cast<uint32>(ETaskEventType::Prerequisite)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Scheduled)] = FLinearColor::Blue;
	ColorCode[static_cast<uint32>(ETaskEventType::Started)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::AddedNested)] = FLinearColor::Blue;
	ColorCode[static_cast<uint32>(ETaskEventType::NestedCompleted)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Subsequent)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Completed)] = FLinearColor::Yellow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTaskGraphProfilerManager::GetColorForTaskEvent(ETaskEventType InEvent)
{
	check(InEvent < ETaskEventType::NumTaskEventTypes);
	return ColorCode[static_cast<uint32>(InEvent)];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
