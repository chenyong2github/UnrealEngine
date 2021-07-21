// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextSwitchesTimingTrack.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "TraceServices/Model/ContextSwitches.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ContextSwitches/ContextSwitchesProfilerManager.h"
#include "Insights/ContextSwitches/ViewModels/ContextSwitchTimingEvent.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/InsightsManager.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/ViewModels/ThreadTrackEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/Widgets/STimingView.h"

using namespace TraceServices;

#define LOCTEXT_NAMESPACE "ContextSwitchesTrack"

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
		UI_COMMAND(Command_ShowCoreTracks, "Core Tracks", "Show/hide the Cpu Core tracks.", EUserInterfaceActionType::ToggleButton, FInputChord());
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
	, bAreContextSwitchesVisible(true)
	, bAreCoreTracksVisible(true)
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
		//TODO: InMenuBuilder.AddMenuEntry(FContextSwitchesStateCommands::Get().Command_ShowCoreTracks);
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
	//TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::RemoveCoreTracks()
{
	//TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::AddContextSwitchesChildTracks()
{
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
// FContextSwitchesTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FContextSwitchesTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}
	
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const IContextSwitchProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchProvider(*Session.Get());

	if (ContextSwitchesProvider == nullptr)
	{
		return;
	}

	const FTimingTrackViewport& Viewport = Context.GetViewport();

	ContextSwitchesProvider->EnumerateContextSwitches(ThreadId, Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder](const FContextSwitch& ContextSwitch)
		{
			Builder.AddEvent(ContextSwitch.Start, ContextSwitch.End, 0, *FString::Printf(TEXT("Core %d"), ContextSwitch.CoreNumber), 0, 0);
			return EContextSwitchEnumerationResult::Continue;
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	BuildDrawState(Builder, Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	DrawLineEvents(Context, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::DrawLineEvents(const ITimingTrackDrawContext& Context, const float OffsetY) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());

	if (Context.GetEventFilter().IsValid() || HasCustomFilter())
	{
		Helper.DrawFadedLineEvents(GetDrawState(), *this, OffsetY, 0.1f);

		if (UpdateFilteredDrawStateOpacity())
		{
			Helper.DrawLineEvents(GetFilteredDrawState(), *this, OffsetY);
		}
		else
		{
			Helper.DrawFadedLineEvents(GetFilteredDrawState(), *this, OffsetY, GetFilteredDrawStateOpacity());
		}
	}
	else
	{
		Helper.DrawLineEvents(GetDrawState(), *this, OffsetY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	if (SharedState.AreOverlaysVisible())
	{
		float LineY1 = 0.0f;
		float LineY2 = 0.0f;
		ETimingTrackLocation LocalLocation = ETimingTrackLocation::None;
		TSharedPtr<FBaseTimingTrack> LocalParentTrack = GetParentTrack().Pin();
		if (LocalParentTrack)
		{
			LineY1 = LocalParentTrack->GetPosY();
			LineY2 = LineY1 + LocalParentTrack->GetHeight();
			LocalLocation = LocalParentTrack->GetLocation();
		}
		else
		{
			LineY1 = GetPosY();
			LineY2 = LineY1 + GetHeight();
			LocalLocation = GetLocation();
		}

		const FTimingTrackViewport& Viewport = Context.GetViewport();
		switch (LocalLocation)
		{
			case ETimingTrackLocation::Scrollable:
			{
				const float TopY = Viewport.GetTopOffset();
				if (LineY1 < TopY)
				{
					LineY1 = TopY;
				}
				const float BottomY = Viewport.GetHeight() - Viewport.GetBottomOffset();
				if (LineY2 > BottomY)
				{
					LineY2 = BottomY;
				}
				break;
			}
			case ETimingTrackLocation::TopDocked:
			{
				const float TopY = 0.0f;
				if (LineY1 < TopY)
				{
					LineY1 = TopY;
				}
				const float BottomY = Viewport.GetTopOffset();
				if (LineY2 > BottomY)
				{
					LineY2 = BottomY;
				}
				break;
			}
			case ETimingTrackLocation::BottomDocked:
			{
				const float TopY = Viewport.GetHeight() - Viewport.GetBottomOffset();
				if (LineY1 < TopY)
				{
					LineY1 = TopY;
				}
				const float BottomY = Viewport.GetHeight();
				if (LineY2 > BottomY)
				{
					LineY2 = BottomY;
				}
				break;
			}
		}

		const float LineH = LineY2 - LineY1;
		if (LineH > 0.0f)
		{
			const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
			Helper.DrawContextSwitchMarkers(GetDrawState(), LineY1, LineH, 0.25f, SharedState.AreExtendedLinesVisible());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FContextSwitchesTimingTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	TSharedPtr<FContextSwitchTimingEvent> TimingEvent;

	const FTimingViewLayout& Layout = Viewport.GetLayout();

	float TopLaneY = 0.0f;
	float TrackLanesHeight = 0.0f;
	if (ChildTrack.IsValid())
	{
		const float HeaderDY = InPosY - ChildTrack->GetPosY();
		if (HeaderDY >= 0 && HeaderDY < ChildTrack->GetHeight())
		{
			return ChildTrack->GetEvent(InPosX, InPosY, Viewport);
		}
		TopLaneY = GetPosY() + 1.0f + Layout.TimelineDY + ChildTrack->GetHeight() + Layout.ChildTimelineDY;
		TrackLanesHeight = GetHeight() - ChildTrack->GetHeight() - 1.0f - 2 * Layout.TimelineDY - Layout.ChildTimelineDY;
	}
	else
	{
		if (IsChildTrack())
		{
			TopLaneY = GetPosY();
			TrackLanesHeight = GetHeight();
		}
		else
		{
			TopLaneY = GetPosY() + 1.0f + Layout.TimelineDY;
			TrackLanesHeight = GetHeight() - 1.0f - 2 * Layout.TimelineDY;
		}
	}

	const float DY = InPosY - TopLaneY;

	// If mouse is not above first sub-track or below last sub-track...
	if (DY >= 0 && DY < TrackLanesHeight)
	{
		const int32 Depth = DY / (Layout.EventH + Layout.EventDY);

		const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();

		const double EventTime = Viewport.SlateUnitsToTime(InPosX);

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

		if (!Session.IsValid())
		{
			return TimingEvent;
		}

		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const IContextSwitchProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchProvider(*Session.Get());

		if (ContextSwitchesProvider == nullptr)
		{
			return TimingEvent;
		}

		FContextSwitch BestMatchContextSwitch;
		double Delta = 2 * SecondsPerPixel;

		ContextSwitchesProvider->EnumerateContextSwitches(ThreadId, EventTime - 2 * SecondsPerPixel, EventTime + 2 * SecondsPerPixel,
			[EventTime, &BestMatchContextSwitch, &Delta](const FContextSwitch& ContextSwitch)
			{
				if (ContextSwitch.Start <= EventTime && ContextSwitch.End >= EventTime)
				{
					BestMatchContextSwitch = ContextSwitch;
					Delta = 0.0f;
					return EContextSwitchEnumerationResult::Stop;
				}

				if (ContextSwitch.End <= EventTime)
				{
					if (EventTime - ContextSwitch.End < Delta)
					{
						Delta = EventTime - ContextSwitch.End;
						BestMatchContextSwitch = ContextSwitch;
					}
				}

				if (ContextSwitch.Start >= EventTime)
				{
					if (ContextSwitch.Start - EventTime < Delta)
					{
						Delta = ContextSwitch.Start - EventTime;
						BestMatchContextSwitch = ContextSwitch;
					}
				}

				return EContextSwitchEnumerationResult::Continue;
			});

		if (Delta < 2 * SecondsPerPixel)
		{
			TimingEvent = MakeShared<FContextSwitchTimingEvent>(SharedThis(this), BestMatchContextSwitch.Start, BestMatchContextSwitch.End, 0);
			TimingEvent->SetCoreNumber(BestMatchContextSwitch.CoreNumber);
		}
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	if (!InTooltipEvent.CheckTrack(this) || !InTooltipEvent.Is<FContextSwitchTimingEvent>())
	{
		return;
	}

	const FContextSwitchTimingEvent& ContextSwitchEvent = InTooltipEvent.As<FContextSwitchTimingEvent>();
	InOutTooltip.AddTitle(FString::Printf(TEXT("Core %d"), ContextSwitchEvent.GetCoreNumber()));

	InOutTooltip.AddNameValueTextLine(TEXT("Start Time:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetStartTime(), 6));
	InOutTooltip.AddNameValueTextLine(TEXT("End Time:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetEndTime(), 6));
	InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetDuration(), 6));

	InOutTooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
