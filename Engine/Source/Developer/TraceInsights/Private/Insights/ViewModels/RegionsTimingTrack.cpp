// Copyright Epic Games, Inc. All Rights Reserved.

#include "RegionsTimingTrack.h"

#include "TimingViewDrawHelper.h"
#include "Common/ProviderLock.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/Log.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/Widgets/STimingView.h"
#include "TraceServices/Model/Regions.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "RegionsTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFileActivityTimingViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingRegionsViewCommands::FTimingRegionsViewCommands()
: TCommands<FTimingRegionsViewCommands>(
	TEXT("FTimingRegionsViewCommands"),
	NSLOCTEXT("Contexts", "FTimingRegionsViewCommands", "Insights - Timing View - Timing Regions"),
	NAME_None,
	FInsightsStyle::GetStyleSetName())
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingRegionsViewCommands::~FTimingRegionsViewCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
UE_DISABLE_OPTIMIZATION_SHIP
void FTimingRegionsViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowHideRegionTrack,
		"Timing Regions Track",
		"Shows/hides the Timing Regions track.",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EKeys::R));
}
UE_ENABLE_OPTIMIZATION_SHIP

////////////////////////////////////////////////////////////////////////////////////////////////////




void FTimingRegionsSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	TimingRegionsTrack.Reset();
}

void FTimingRegionsSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	TimingRegionsTrack.Reset();
}

void FTimingRegionsSharedState::Tick(Insights::ITimingViewSession& InSession,
	const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (!TimingRegionsTrack.IsValid())
	{
		TimingRegionsTrack = MakeShared<FTimingRegionsTrack>(*this);
		TimingRegionsTrack->SetOrder(FTimingTrackOrder::First);
		TimingRegionsTrack->SetVisibilityFlag(true);
		InSession.AddScrollableTrack(TimingRegionsTrack);
	}
}

void FTimingRegionsSharedState::ShowHideRegionsTrack()
{
	bShowHideRegionsTrack = !bShowHideRegionsTrack;

	if (TimingRegionsTrack.IsValid())
	{
		TimingRegionsTrack->SetVisibilityFlag(bShowHideRegionsTrack);
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}

	if (bShowHideRegionsTrack)
	{
		TimingRegionsTrack->SetDirtyFlag();
	}
}

void FTimingRegionsSharedState::ExtendOtherTracksFilterMenu(Insights::ITimingViewSession& InSession,
                                                            FMenuBuilder& InOutMenuBuilder)
{
	InOutMenuBuilder.BeginSection("Timing Regions", LOCTEXT("ContextMenu_Section_Regions", "Timing Regions"));
	{
		// Note: We use the custom AddMenuEntry in order to set the same key binding text for multiple menu items.

		//InOutMenuBuilder.AddMenuEntry(FFileActivityTimingViewCommands::Get().ShowHideIoOverviewTrack);
		FInsightsMenuBuilder::AddMenuEntry(InOutMenuBuilder,
			FUIAction(
				FExecuteAction::CreateSP(this, &FTimingRegionsSharedState::ShowHideRegionsTrack),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FTimingRegionsSharedState::IsRegionsTrackVisible)),
			FTimingRegionsViewCommands::Get().ShowHideRegionTrack->GetLabel(),
			FTimingRegionsViewCommands::Get().ShowHideRegionTrack->GetDescription(),
			LOCTEXT("TimingRegionsTracksKeybinding", "R"),
			EUserInterfaceActionType::ToggleButton);
	}
	InOutMenuBuilder.EndSection();
}

void FTimingRegionsSharedState::BindCommands()
{
	FTimingRegionsViewCommands::Register();

	TSharedPtr<FUICommandList> CommandList = TimingView->GetCommandList();
	ensure(CommandList.IsValid());

	CommandList->MapAction(
		FTimingRegionsViewCommands::Get().ShowHideRegionTrack,
		FExecuteAction::CreateSP(this, &FTimingRegionsSharedState::ShowHideRegionsTrack),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTimingRegionsSharedState::IsRegionsTrackVisible));
}


//////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTimingRegionsTrack)

FTimingRegionsTrack::~FTimingRegionsTrack()
{
}

void FTimingRegionsTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	FTimingEventsTrack::BuildContextMenu(MenuBuilder);
}


void FTimingRegionsTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && InTooltipEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TooltipEvent = InTooltipEvent.As<FTimingEvent>();

		auto MatchEvent = [this, &TooltipEvent](double InStartTime, double InEndTime, uint32 InDepth)
		{
			return InDepth == TooltipEvent.GetDepth()
				&& InStartTime == TooltipEvent.GetStartTime()
				&& InEndTime == TooltipEvent.GetEndTime();
		};

		FTimingEventSearchParameters SearchParameters(TooltipEvent.GetStartTime(), TooltipEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
		FindRegionEvent(SearchParameters, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const TraceServices::FTimeRegion& InRegion)
		{
			InOutTooltip.Reset();
			InOutTooltip.AddTitle(InRegion.Text, FLinearColor::White);
			InOutTooltip.AddNameValueTextLine(TEXT("Duration:"),  TimeUtils::FormatTimeAuto(InRegion.EndTime-InRegion.BeginTime));
			InOutTooltip.AddNameValueTextLine(TEXT("Depth:"),  FString::FromInt(InRegion.Depth));
			InOutTooltip.UpdateLayout();
		});
	}
}

void FTimingRegionsTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder,
	const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

	const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
	TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

	FStopwatch Stopwatch;
	Stopwatch.Start();
	
	// whe're counting only non-empty lanes, so we can collapse empty ones in the visualization.
	int32 CurDepth = 0;
	RegionProvider.EnumerateLanes([this, Viewport, &CurDepth, &Builder](const TraceServices::FRegionLane& Lane, const int32 Depth)
	{
		bool RegionHadEvents = false;
		Lane.EnumerateRegions(Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder, &RegionHadEvents, &CurDepth](const TraceServices::FTimeRegion& Region) -> bool
		{
			RegionHadEvents = true;
			Builder.AddEvent(Region.BeginTime, Region.EndTime,CurDepth, Region.Text);
			return true;
		});

		if (RegionHadEvents) CurDepth++;
	});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_CLOG(TotalTime > 1.0,TimingProfiler, Verbose, TEXT("[Regions] Updated draw state in %s."), *TimeUtils::FormatTimeAuto(TotalTime));
}

const TSharedPtr<const ITimingEvent> FTimingRegionsTrack::SearchEvent(
	const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindRegionEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const TraceServices::FTimeRegion& InEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

bool FTimingRegionsTrack::FindRegionEvent(const FTimingEventSearchParameters& InParameters,
	TFunctionRef<void(double, double, uint32, const TraceServices::FTimeRegion&)> InFoundPredicate) const
{
	return TTimingEventSearch<TraceServices::FTimeRegion>::Search(
	InParameters,

	// Search...
	[this](TTimingEventSearch<TraceServices::FTimeRegion>::FContext& InContext)
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
			TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

		RegionProvider.EnumerateRegions(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](const TraceServices::FTimeRegion& Region)
		{
			InContext.Check(Region.BeginTime, Region.EndTime, Region.Depth, Region);
			
			if (!InContext.ShouldContinueSearching())
			{
				return false;
			}
			
			return true;
		});
	},
	// Found!
	InFoundPredicate);
}

#undef LOCTEXT_NAMESPACE