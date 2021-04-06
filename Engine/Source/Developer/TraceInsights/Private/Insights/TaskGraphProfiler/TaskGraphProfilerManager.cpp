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

} // namespace Insights

#undef LOCTEXT_NAMESPACE
