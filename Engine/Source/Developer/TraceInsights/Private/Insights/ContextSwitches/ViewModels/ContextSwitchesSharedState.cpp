// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextSwitchesSharedState.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "TraceServices/Model/ContextSwitches.h"

// Insights
#include "Insights/ContextSwitches/ContextSwitchesProfilerManager.h"
#include "Insights/ContextSwitches/ViewModels/ContextSwitchesTimingTrack.h"
#include "Insights/ContextSwitches/ViewModels/CpuCoreTimingTrack.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/InsightsManager.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "ContextSwitches"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FContextSwitchesStateCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FContextSwitchesStateCommands : public TCommands<FContextSwitchesStateCommands>
{
public:
	FContextSwitchesStateCommands()
		: TCommands<FContextSwitchesStateCommands>(TEXT("FContextSwitchesStateCommands"), NSLOCTEXT("FContextSwitchesStateCommands", "Context Switches State Commands", "Context Switches Commands"), NAME_None, FEditorStyle::Get().GetStyleSetName())
	{
	}

	virtual ~FContextSwitchesStateCommands()
	{
	}

	// UI_COMMAND takes long for the compiler to optimize
	PRAGMA_DISABLE_OPTIMIZATION
	virtual void RegisterCommands() override
	{
		UI_COMMAND(Command_ShowCoreTracks, "Core Tracks", "Show/hide the Cpu Core tracks.", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Alt, EKeys::C));
		UI_COMMAND(Command_ShowContextSwitches, "Context Switches", "Show/hide context switches on top of cpu timing tracks.", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::C));
		UI_COMMAND(Command_ShowOverlays, "Overlays", "Extend the visualisation of context switches over the cpu timing tracks.", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::O));
		UI_COMMAND(Command_ShowExtendedLines, "Extended Lines", "Show/hide the extended vertical lines at edges of each context switch event.", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::L));
	}
	PRAGMA_ENABLE_OPTIMIZATION

	TSharedPtr<FUICommandInfo> Command_ShowCoreTracks;
	TSharedPtr<FUICommandInfo> Command_ShowContextSwitches;
	TSharedPtr<FUICommandInfo> Command_ShowOverlays;
	TSharedPtr<FUICommandInfo> Command_ShowExtendedLines;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FContextSwitchesSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

FContextSwitchesSharedState::FContextSwitchesSharedState(STimingView* InTimingView) 
	: TimingView(InTimingView)
	, ThreadsSerial(0)
	, CpuCoresSerial(0)
	, bAreCoreTracksVisible(false)
	, bAreContextSwitchesVisible(true)
	, bAreOverlaysVisible(true)
	, bAreExtendedLinesVisible(true)
	, bSyncWithProviders(true)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	ThreadsSerial = 0;
	CpuCoresSerial = 0;

	bAreCoreTracksVisible = true;
	bAreContextSwitchesVisible = true;
	bAreOverlaysVisible = true;
	bAreExtendedLinesVisible = true;

	bSyncWithProviders = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	ThreadsSerial = 0;
	CpuCoresSerial = 0;

	bAreCoreTracksVisible = true;
	bAreContextSwitchesVisible = true;
	bAreOverlaysVisible = true;
	bAreExtendedLinesVisible = true;

	bSyncWithProviders = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (bSyncWithProviders && AreContextSwitchesAvailable())
	{
		if (bAreCoreTracksVisible)
		{
			AddCoreTracks();
		}

		if (bAreContextSwitchesVisible)
		{
			AddContextSwitchesChildTracks();
		}

		if (FInsightsManager::Get()->IsAnalysisComplete())
		{
			// No need to sync anymore when analysis is completed.
			bSyncWithProviders = false;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InMenuBuilder.BeginSection("ContextSwitches");
	{
		InMenuBuilder.AddSubMenu(
			LOCTEXT("ContextSwitches_SubMenu", "Context Switches"),
			LOCTEXT("ContextSwitches_SubMenu_Desc", "Context Switch track options"),
			FNewMenuDelegate::CreateSP(this, &FContextSwitchesSharedState::BuildSubMenu),
			false,
			FSlateIcon()
		);
	}
	InMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::BuildSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("ContextSwitches", LOCTEXT("ContextSwitchesHeading", "Context Switches"));
	{
		InMenuBuilder.AddMenuEntry(FContextSwitchesStateCommands::Get().Command_ShowCoreTracks);
		InMenuBuilder.AddMenuEntry(FContextSwitchesStateCommands::Get().Command_ShowContextSwitches);
		InMenuBuilder.AddMenuEntry(FContextSwitchesStateCommands::Get().Command_ShowOverlays);
		InMenuBuilder.AddMenuEntry(FContextSwitchesStateCommands::Get().Command_ShowExtendedLines);
	}
	InMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::AddCommands()
{
	FContextSwitchesStateCommands::Register();

	TSharedPtr<FUICommandList> CommandList = TimingView->GetCommandList();
	ensure(CommandList.IsValid());

	CommandList->MapAction(
		FContextSwitchesStateCommands::Get().Command_ShowCoreTracks,
		FExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowCoreTracks_Execute),
		FCanExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowCoreTracks_CanExecute),
		FIsActionChecked::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowCoreTracks_IsChecked));

	CommandList->MapAction(
		FContextSwitchesStateCommands::Get().Command_ShowContextSwitches,
		FExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_Execute),
		FCanExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_CanExecute),
		FIsActionChecked::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_IsChecked));

	CommandList->MapAction(
		FContextSwitchesStateCommands::Get().Command_ShowOverlays,
		FExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowOverlays_Execute),
		FCanExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowOverlays_CanExecute),
		FIsActionChecked::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowOverlays_IsChecked));

	CommandList->MapAction(
		FContextSwitchesStateCommands::Get().Command_ShowExtendedLines,
		FExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowExtendedLines_Execute),
		FCanExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowExtendedLines_CanExecute),
		FIsActionChecked::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowExtendedLines_IsChecked));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FContextSwitchesSharedState::AreContextSwitchesAvailable() const
{
	return FContextSwitchesProfilerManager::Get()->IsAvailable();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::AddCoreTracks()
{
	if (!TimingView)
	{
		return;
	}

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
	if (ContextSwitchesProvider == nullptr)
	{
		return;
	}

	const uint64 NewCpuCoresSerial = ContextSwitchesProvider->GetCpuCoresSerial();
	if (NewCpuCoresSerial == CpuCoresSerial)
	{
		return;
	}

	CpuCoresSerial = NewCpuCoresSerial;

	ContextSwitchesProvider->EnumerateCpuCores([this](const TraceServices::FCpuCoreInfo& CpuCoreInfo)
		{
			TSharedPtr<FCpuCoreTimingTrack>* TrackPtrPtr = CpuCoreTimingTracks.Find(CpuCoreInfo.CoreNumber);
			if (TrackPtrPtr == nullptr)
			{
				const FString TrackName = FString::Printf(TEXT("Core %u"), CpuCoreInfo.CoreNumber);
				TSharedPtr<FCpuCoreTimingTrack> Track = MakeShared<FCpuCoreTimingTrack>(*this, TrackName, CpuCoreInfo.CoreNumber);

				const int32 Order = FTimingTrackOrder::Cpu - 1024 + CpuCoreInfo.CoreNumber;
				Track->SetOrder(Order);

				CpuCoreTimingTracks.Add(CpuCoreInfo.CoreNumber, Track);
				TimingView->AddScrollableTrack(Track);
			}
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::RemoveCoreTracks()
{
	CpuCoresSerial = 0;

	if (TimingView)
	{
		for (const auto& KV : CpuCoreTimingTracks)
		{
			TimingView->RemoveTrack(KV.Value);
		}
	}

	CpuCoreTimingTracks.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::AddContextSwitchesChildTracks()
{
#if 0
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());

	if (ContextSwitchesProvider == nullptr)
	{
		return;
	}

	const uint64 NewThreadsSerial = ContextSwitchesProvider->GetThreadsSerial();
	if (NewThreadsSerial == ThreadsSerial)
	{
		return;
	}

	ThreadsSerial = NewThreadsSerial;

	//TODO: Create "Cpu Thread" timing tracks also for threads without cpu timing events (i.e. only with context switch events).
#endif

	TSharedPtr<FThreadTimingSharedState> TimingSharedState = TimingView->GetThreadTimingSharedState();

	if (!TimingSharedState.IsValid())
	{
		return;
	}

	const TMap<uint32, TSharedPtr<FCpuTimingTrack>>& CpuTracks = TimingSharedState->GetAllCpuTracks();

	for (const TPair<uint32, TSharedPtr<FCpuTimingTrack>>& MapEntry : CpuTracks)
	{
		const TSharedPtr<FCpuTimingTrack>& CpuTrack = MapEntry.Value;
		if (CpuTrack.IsValid() && !CpuTrack->GetChildTrack().IsValid())
		{
			TSharedPtr<FContextSwitchesTimingTrack> ContextSwitchesTrack = MakeShared<FContextSwitchesTimingTrack>(*this, TEXT("Context Switches"), CpuTrack->GetTimelineIndex(), CpuTrack->GetThreadId());
			ContextSwitchesTrack->SetParentTrack(CpuTrack);
			CpuTrack->SetChildTrack(ContextSwitchesTrack);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::RemoveContextSwitchesChildTracks()
{
	ThreadsSerial = 0;

	TSharedPtr<FThreadTimingSharedState> TimingSharedState = TimingView->GetThreadTimingSharedState();

	if (!TimingSharedState.IsValid())
	{
		return;
	}

	const TMap<uint32, TSharedPtr<FCpuTimingTrack>>& CpuTracks = TimingSharedState->GetAllCpuTracks();

	for (const TPair<uint32, TSharedPtr<FCpuTimingTrack>>& MapEntry : CpuTracks)
	{
		const TSharedPtr<FCpuTimingTrack>& CpuTrack = MapEntry.Value;
		if (CpuTrack.IsValid() && CpuTrack->GetChildTrack().IsValid() && CpuTrack->GetChildTrack()->Is<FContextSwitchesTimingTrack>())
		{
			CpuTrack->SetChildTrack(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::SetCoreTracksVisible(bool bOnOff)
{
	if (bAreCoreTracksVisible != bOnOff)
	{
		bAreCoreTracksVisible = bOnOff;

		if (bAreCoreTracksVisible)
		{
			AddCoreTracks();
		}
		else
		{
			RemoveCoreTracks();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::SetContextSwitchesVisible(bool bOnOff)
{
	if (bAreContextSwitchesVisible != bOnOff)
	{
		bAreContextSwitchesVisible = bOnOff;

		if (bAreContextSwitchesVisible)
		{
			AddContextSwitchesChildTracks();
		}
		else
		{
			RemoveContextSwitchesChildTracks();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::SetOverlaysVisible(bool bOnOff)
{
	bAreOverlaysVisible = bOnOff;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::SetExtendedLinesVisible(bool bOnOff)
{
	bAreExtendedLinesVisible = bOnOff;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
