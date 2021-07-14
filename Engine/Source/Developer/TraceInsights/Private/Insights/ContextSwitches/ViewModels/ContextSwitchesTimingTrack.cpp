// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextSwitchesTimingTrack.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "TraceServices/Model/ContextSwitches.h"

// Insights
#include "Insights/Common/TimeUtils.h"
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
		UI_COMMAND(Command_ShowContextSwitches, "Show Context Switches ", "Show/hide context switches events as header tracks", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::C));
	}
	PRAGMA_ENABLE_OPTIMIZATION

	TSharedPtr<FUICommandInfo> Command_ShowContextSwitches;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FContextSwitchesSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

FContextSwitchesSharedState::FContextSwitchesSharedState(STimingView* InTimingView) 
	: TimingView(InTimingView) 
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	AddContextSwitchesChildTracks();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (!FInsightsManager::Get()->IsAnalysisComplete() && bShowContextSwitchesTrack)
	{
		AddContextSwitchesChildTracks();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FContextSwitchesSharedState::ExtendGlobalContextMenu(ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddMenuEntry
	(
		FContextSwitchesStateCommands::Get().Command_ShowContextSwitches,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon()
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::AddCommands()
{
	FContextSwitchesStateCommands::Register();

	TSharedPtr<FUICommandList> CommandList = TimingView->GetCommandList();
	ensure(CommandList.IsValid());

	CommandList->MapAction(
		FContextSwitchesStateCommands::Get().Command_ShowContextSwitches,
		FExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_Execute),
		FCanExecuteAction::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_CanExecute),
		FIsActionChecked::CreateSP(this, &FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_IsChecked));
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
		if (MapEntry.Value.IsValid() && !MapEntry.Value->GetChildTrack().IsValid())
		{
			TSharedPtr<FContextSwitchesTimingTrack> ContextSwitchesTrack = MakeShared<FContextSwitchesTimingTrack>(*this, TEXT("Context Switches"), MapEntry.Value->GetTimelineIndex(), MapEntry.Value->GetThreadId());
			ContextSwitchesTrack->SetParentTrack(MapEntry.Value);
			MapEntry.Value->SetChildTrack(ContextSwitchesTrack);
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
		if (MapEntry.Value.IsValid() && MapEntry.Value->GetChildTrack().IsValid() && MapEntry.Value->GetChildTrack()->Is<FContextSwitchesTimingTrack>())
		{
			MapEntry.Value->SetChildTrack(nullptr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_Execute()
{
	SetContextSwitchesToggle(!bShowContextSwitchesTrack);

	if (bShowContextSwitchesTrack)
	{
		AddContextSwitchesChildTracks();
	}
	else
	{
		RemoveContextSwitchesChildTracks();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FContextSwitchesSharedState::ContextMenu_ShowContextSwitches_IsChecked()
{
	return IsContextSwitchesToggleOn();
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
		Helper.DrawContextSwitchMarkers(GetDrawState(), LineY1, LineH, 0.25f);
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
