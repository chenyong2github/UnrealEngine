// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextSwitchesTimingTrack.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "TraceServices/Model/ContextSwitches.h"

// Insights
#include "Insights/Common/TimeUtils.h"
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
	}
	PRAGMA_ENABLE_OPTIMIZATION
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
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

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

void FContextSwitchesSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesSharedState::InitCommandList()
{
	FContextSwitchesStateCommands::Register();
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

	const TPagedArray<FContextSwitch>* ContextSwitches = ContextSwitchesProvider->GetContextSwitches(ThreadId);

	if (ContextSwitches == nullptr)
	{
		return;
	}

	const FTimingTrackViewport& Viewport = Context.GetViewport();

	uint64 ContextSwitchPageIndex = Algo::UpperBoundBy(*ContextSwitches, Viewport.GetStartTime(), [](const TPagedArrayPage<FContextSwitch>& ContextSwitchPage)
		{
			return ContextSwitchPage.Items[0].Start;
		});

	if (ContextSwitchPageIndex > 0)
	{
		--ContextSwitchPageIndex;
	}

	auto Iterator = ContextSwitches->GetIteratorFromPage(ContextSwitchPageIndex);
	const FContextSwitch* CurrentContextSwitch = Iterator.NextItem();
	while (CurrentContextSwitch && CurrentContextSwitch->Start < Viewport.GetEndTime())
	{
		if (CurrentContextSwitch->End > Viewport.GetStartTime())
		{
			Builder.AddEvent(CurrentContextSwitch->Start, CurrentContextSwitch->End, 0, *FString::Printf(TEXT("Core %d"), CurrentContextSwitch->CoreNumber), 0, 0);
		}

		CurrentContextSwitch = Iterator.NextItem();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	BuildDrawState(Builder, Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FContextSwitchesTimingTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	TSharedPtr<FTimingEvent> TimingEvent;

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

		const TPagedArray<FContextSwitch>* ContextSwitches = ContextSwitchesProvider->GetContextSwitches(ThreadId);

		if (ContextSwitches == nullptr)
		{
			return TimingEvent;
		}

		uint64 ContextSwitchPageIndex = Algo::UpperBoundBy(*ContextSwitches, EventTime + SecondsPerPixel, [](const TPagedArrayPage<FContextSwitch>& ContextSwitchPage)
			{
				return ContextSwitchPage.Items[0].Start;
			});

		if (ContextSwitchPageIndex > 0)
		{
			--ContextSwitchPageIndex;
		}

		auto Iterator = ContextSwitches->GetIteratorFromPage(ContextSwitchPageIndex);
		const FContextSwitch* CurrentContextSwitch = Iterator.NextItem();
		while (CurrentContextSwitch && CurrentContextSwitch->End < EventTime)
		{
			CurrentContextSwitch = Iterator.NextItem();
		}

		if (CurrentContextSwitch)
		{
			ensure(ParentTrack.IsValid());
			if (CurrentContextSwitch->Start < EventTime && EventTime < CurrentContextSwitch->End)
			{
				TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), CurrentContextSwitch->Start, CurrentContextSwitch->End, 0);
			}
		}
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FContextSwitchesTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	InOutTooltip.UpdateLayout();
}

} // namespace Insights

#undef LOCTEXT_NAMESPACE
