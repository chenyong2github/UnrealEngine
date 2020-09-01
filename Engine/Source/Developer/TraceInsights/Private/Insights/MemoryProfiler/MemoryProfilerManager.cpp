// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/Memory.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/Widgets/SMemoryProfilerWindow.h"
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "MemoryProfilerManager"

DEFINE_LOG_CATEGORY(MemoryProfiler);

TSharedPtr<FMemoryProfilerManager> FMemoryProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryProfilerManager> FMemoryProfilerManager::Get()
{
	return FMemoryProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryProfilerManager> FMemoryProfilerManager::CreateInstance()
{
	ensure(!FMemoryProfilerManager::Instance.IsValid());
	if (FMemoryProfilerManager::Instance.IsValid())
	{
		FMemoryProfilerManager::Instance.Reset();
	}

	FMemoryProfilerManager::Instance = MakeShared<FMemoryProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FMemoryProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryProfilerManager::FMemoryProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
	, AvailabilityCheckNextTimestamp(0)
	, AvailabilityCheckWaitTimeSec(1.0)
	, CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindow(nullptr)
	, bIsTimingViewVisible(false)
	, bIsMemTagTreeViewVisible(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FMemoryProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FMemoryProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FMemoryProfilerCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FMemoryProfilerManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryProfilerManager::~FMemoryProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleTimingViewVisibility_Global();
	ActionManager.Map_ToggleMemTagTreeViewVisibility_Global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	const FInsightsMajorTabConfig& Config = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::MemoryProfilerTabId);

	if (Config.bIsAvailable)
	{
		// Register tab spawner for the Memory Insights.
		FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FInsightsManagerTabs::MemoryProfilerTabId,
			FOnSpawnTab::CreateRaw(this, &FMemoryProfilerManager::SpawnTab), FCanSpawnTab::CreateRaw(this, &FMemoryProfilerManager::CanSpawnTab))
			.SetDisplayName(Config.TabLabel.IsSet() ? Config.TabLabel.GetValue() : LOCTEXT("MemoryProfilerTabTitle", "Memory Insights"))
			.SetTooltipText(Config.TabTooltip.IsSet() ? Config.TabTooltip.GetValue() : LOCTEXT("MemoryProfilerTooltipText", "Open the Memory Insights tab."))
			.SetIcon(Config.TabIcon.IsSet() ? Config.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "MemoryProfiler.Icon.Small"));

		TSharedRef<FWorkspaceItem> Group = Config.WorkspaceGroup.IsValid() ? Config.WorkspaceGroup.ToSharedRef() : FInsightsManager::Get()->GetInsightsMenuBuilder()->GetInsightsToolsGroup();
		TabSpawnerEntry.SetGroup(Group);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::UnregisterMajorTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FInsightsManagerTabs::MemoryProfilerTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FMemoryProfilerManager::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FMemoryProfilerManager::OnTabClosed));

	// Create the SMemoryProfilerWindow widget.
	TSharedRef<SMemoryProfilerWindow> Window = SNew(SMemoryProfilerWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AssignProfilerWindow(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FMemoryProfilerManager::CanSpawnTab(const FSpawnTabArgs& Args) const
{
	return bIsAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::OnTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	RemoveProfilerWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FMemoryProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FMemoryProfilerCommands& FMemoryProfilerManager::GetCommands()
{
	return FMemoryProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryProfilerActionManager& FMemoryProfilerManager::GetActionManager()
{
	return FMemoryProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemorySharedState* FMemoryProfilerManager::GetSharedState()
{
	TSharedPtr<SMemoryProfilerWindow> Wnd = GetProfilerWindow();
	return Wnd.IsValid() ? &Wnd->GetSharedState() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FMemoryProfilerManager::Tick(float DeltaTime)
{
	if (!bIsAvailable)
	{
		// Check if session has Memory events (to spawn the tab), but not too often.
		const uint64 Time = FPlatformTime::Cycles64();
		if (Time > AvailabilityCheckNextTimestamp)
		{
			AvailabilityCheckWaitTimeSec += 1.0; // increase wait time with 1s
			const uint64 WaitTime = static_cast<uint64>(AvailabilityCheckWaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
			AvailabilityCheckNextTimestamp = Time + WaitTime;

			uint32 TagCount = 0;

			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
				const Trace::IMemoryProvider& MemoryProvider = Trace::ReadMemoryProvider(*Session.Get());
				TagCount = MemoryProvider.GetTagCount();
			}

			if (TagCount > 0)
			{
				bIsAvailable = true;
#if !WITH_EDITOR
				const FName& TabId = FInsightsManagerTabs::MemoryProfilerTabId;
				if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
				{
					FGlobalTabmanager::Get()->TryInvokeTab(TabId);
				}
#endif
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::OnSessionChanged()
{
	bIsAvailable = false;
	AvailabilityCheckNextTimestamp = 0;
	AvailabilityCheckWaitTimeSec = 1.0;

	TSharedPtr<SMemoryProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::ShowHideTimingView(const bool bIsVisible)
{
	bIsTimingViewVisible = bIsVisible;

	TSharedPtr<SMemoryProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FMemoryProfilerTabs::TimingViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemoryProfilerManager::ShowHideMemTagTreeView(const bool bIsVisible)
{
	bIsMemTagTreeViewVisible = bIsVisible;

	TSharedPtr<SMemoryProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd.IsValid())
	{
		Wnd->ShowHideTab(FMemoryProfilerTabs::MemTagTreeViewID, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
