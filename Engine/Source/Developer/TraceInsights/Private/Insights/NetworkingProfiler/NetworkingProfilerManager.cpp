// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkingProfilerManager.h"

#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "NetworkingProfilerManager"

DEFINE_LOG_CATEGORY(NetworkingProfiler);

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::Get()
{
	return FNetworkingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FNetworkingProfilerManager> FNetworkingProfilerManager::CreateInstance()
{
	ensure(!FNetworkingProfilerManager::Instance.IsValid());
	if (FNetworkingProfilerManager::Instance.IsValid())
	{
		FNetworkingProfilerManager::Instance.Reset();
	}

	FNetworkingProfilerManager::Instance = MakeShared<FNetworkingProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FNetworkingProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerManager::FNetworkingProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
	, AvailabilityCheckNextTimestamp(0)
	, AvailabilityCheckWaitTimeSec(1.0)
	, CommandList(InCommandList)
	, ActionManager(this)
	, ProfilerWindows()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FNetworkingProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 1.0f);

	FNetworkingProfilerCommands::Register();
	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FNetworkingProfilerCommands::Unregister();

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FNetworkingProfilerManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerManager::~FNetworkingProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::BindCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static TSharedPtr<SDockTab> NeverReuse(const FTabId&)
{
	return nullptr;
}

void FNetworkingProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	const FInsightsMajorTabConfig& Config = InsightsModule.FindMajorTabConfig(FInsightsManagerTabs::NetworkingProfilerTabId);

	if (Config.bIsAvailable)
	{
		// Register tab spawner(s) for the Networking Insights.
		//for (int32 ReservedId = 0; ReservedId < 10; ++ReservedId)
		{
			FName TabId = FInsightsManagerTabs::NetworkingProfilerTabId;
			//TabId.SetNumber(ReservedId);
			FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
				FOnSpawnTab::CreateRaw(this, &FNetworkingProfilerManager::SpawnTab), FCanSpawnTab::CreateRaw(this, &FNetworkingProfilerManager::CanSpawnTab))
				.SetReuseTabMethod(FOnFindTabToReuse::CreateStatic(&NeverReuse))
				.SetDisplayName(Config.TabLabel.IsSet() ? Config.TabLabel.GetValue() : LOCTEXT("NetworkingProfilerTabTitle", "Networking Insights"))
				.SetTooltipText(Config.TabTooltip.IsSet() ? Config.TabTooltip.GetValue() : LOCTEXT("NetworkingProfilerTooltipText", "Open the Networking Insights tab."))
				.SetIcon(Config.TabIcon.IsSet() ? Config.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "NetworkingProfiler.Icon.Small"));

			TSharedRef<FWorkspaceItem> Group = Config.WorkspaceGroup.IsValid() ? Config.WorkspaceGroup.ToSharedRef() : FInsightsManager::Get()->GetInsightsMenuBuilder()->GetInsightsToolsGroup();
			TabSpawnerEntry.SetGroup(Group);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::UnregisterMajorTabs()
{
	//for (int32 ReservedId = 0; ReservedId < 10; ++ReservedId)
	{
		FName TabId = FInsightsManagerTabs::NetworkingProfilerTabId;
		//TabId.SetNumber(ReservedId);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FNetworkingProfilerManager::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle I/O profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FNetworkingProfilerManager::OnTabClosed));

	// Create the SNetworkingProfilerWindow widget.
	TSharedRef<SNetworkingProfilerWindow> Window = SNew(SNetworkingProfilerWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AddProfilerWindow(Window);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FNetworkingProfilerManager::CanSpawnTab(const FSpawnTabArgs& Args) const
{
	return bIsAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::OnTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	TSharedRef<SNetworkingProfilerWindow> Window = StaticCastSharedRef<SNetworkingProfilerWindow>(TabBeingClosed->GetContent());

	RemoveProfilerWindow(Window);

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FNetworkingProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FNetworkingProfilerCommands& FNetworkingProfilerManager::GetCommands()
{
	return FNetworkingProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkingProfilerActionManager& FNetworkingProfilerManager::GetActionManager()
{
	return FNetworkingProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FNetworkingProfilerManager::Tick(float DeltaTime)
{
	if (!bIsAvailable)
	{
		// Check if session has Networking events (to spawn the tab), but not too often.
		const uint64 Time = FPlatformTime::Cycles64();
		if (Time > AvailabilityCheckNextTimestamp)
		{
			AvailabilityCheckWaitTimeSec += 1.0; // increase wait time with 1s
			const uint64 WaitTime = static_cast<uint64>(AvailabilityCheckWaitTimeSec / FPlatformTime::GetSecondsPerCycle64());
			AvailabilityCheckNextTimestamp = Time + WaitTime;

			uint32 NetTraceVersion = 0;

			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
				const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());
				NetTraceVersion = NetProfilerProvider.GetNetTraceVersion();
			}

			if (NetTraceVersion > 0)
			{
				bIsAvailable = true;

				const FName& TabId = FInsightsManagerTabs::NetworkingProfilerTabId;
				if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
				{
					FGlobalTabmanager::Get()->TryInvokeTab(TabId);
					FGlobalTabmanager::Get()->TryInvokeTab(TabId);
				}

				//int32 SpawnTabCount = 2; // we want to spawn 2 tabs
				//for (int32 ReservedId = 0; SpawnTabCount > 0 && ReservedId < 10; ++ReservedId)
				//{
				//	FName TabId = FInsightsManagerTabs::NetworkingProfilerTabId;
				//	TabId.SetNumber(ReservedId);
				//
				//	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId) && 
				//		!FGlobalTabmanager::Get()->FindExistingLiveTab(TabId).IsValid())
				//	{
				//		FGlobalTabmanager::Get()->TryInvokeTab(TabId);
				//		--SpawnTabCount;
				//	}
				//}

				// ActivateTimingInsightsTab();
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetworkingProfilerManager::OnSessionChanged()
{
	bIsAvailable = false;
	AvailabilityCheckNextTimestamp = 0;
	AvailabilityCheckWaitTimeSec = 1.0;

	for (TWeakPtr<SNetworkingProfilerWindow> WndWeakPtr : ProfilerWindows)
	{
		TSharedPtr<SNetworkingProfilerWindow> Wnd = WndWeakPtr.Pin();
		if (Wnd.IsValid())
		{
			Wnd->Reset();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
