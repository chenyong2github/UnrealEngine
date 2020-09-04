// Copyright Epic Games, Inc. All Rights Reserved.

#include "LoadingTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "LoadingTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (TimingView && TimingView->IsAssetLoadingModeEnabled())
	{
		bShowHideAllLoadingTracks = true;
	}
	else
	{
		bShowHideAllLoadingTracks = false;
	}

	LoadingTracks.Reset();

	LoadTimeProfilerTimelineCount = 0;

	SetColorSchema(3);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	bShowHideAllLoadingTracks = false;

	LoadingTracks.Reset();

	LoadTimeProfilerTimelineCount = 0;

	GetEventNameDelegate = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	const Trace::ILoadTimeProfilerProvider* LoadTimeProfilerProvider = Trace::ReadLoadTimeProfilerProvider(InAnalysisSession);
	if (LoadTimeProfilerProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

		const uint64 CurrentLoadTimeProfilerTimelineCount = LoadTimeProfilerProvider->GetTimelineCount();
		if (CurrentLoadTimeProfilerTimelineCount != LoadTimeProfilerTimelineCount)
		{
			LoadTimeProfilerTimelineCount = CurrentLoadTimeProfilerTimelineCount;

			// Iterate through threads.
			const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(InAnalysisSession);
			ThreadProvider.EnumerateThreads([this, &InSession, LoadTimeProfilerProvider](const Trace::FThreadInfo& ThreadInfo)
			{
				// Check available Asset Loading tracks.
				uint32 LoadingTimelineIndex;
				if (LoadTimeProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, LoadingTimelineIndex))
				{
					if (!LoadingTracks.Contains(LoadingTimelineIndex))
					{
						//const TCHAR* const GroupName = ThreadInfo.GroupName ? ThreadInfo.GroupName : ThreadInfo.Name;
						const FString TrackName(ThreadInfo.Name && *ThreadInfo.Name ? FString::Printf(TEXT("Loading - %s"), ThreadInfo.Name) : FString::Printf(TEXT("Loading - Thread %u"), ThreadInfo.Id));
						TSharedRef<FLoadingTimingTrack> LoadingThreadTrack = MakeShared<FLoadingTimingTrack>(*this, LoadingTimelineIndex, TrackName);
						static_assert(FTimingTrackOrder::GroupRange > 1000, "Order group range too small");
						LoadingThreadTrack->SetOrder(FTimingTrackOrder::Cpu - 1000 + LoadingTracks.Num() * 10);
						LoadingThreadTrack->SetVisibilityFlag(bShowHideAllLoadingTracks);
						InSession.AddScrollableTrack(LoadingThreadTrack);
						LoadingTracks.Add(LoadingTimelineIndex, LoadingThreadTrack);
					}
				}
			});
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("Asset Loading", LOCTEXT("AssetLoadingHeading", "Asset Loading"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllLoadingTracks", "Asset Loading Tracks - L"),
			LOCTEXT("ShowAllLoadingTracks_Tooltip", "Show/hide the Asset Loading tracks"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FLoadingSharedState::ShowHideAllLoadingTracks),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FLoadingSharedState::IsAllLoadingTracksToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::SetAllLoadingTracksToggle(bool bOnOff)
{
	bShowHideAllLoadingTracks = bOnOff;

	for (const auto& KV : LoadingTracks)
	{
		FLoadingTimingTrack& Track = *KV.Value;
		Track.SetVisibilityFlag(bShowHideAllLoadingTracks);
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return Trace::GetLoadTimeProfilerObjectEventTypeString(Event.EventType);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByPackageName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return Event.Package ? Event.Package->Name : TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return Event.Export && Event.Export->Class ? Event.Export->Class->Name : TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByPackageAndExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	if (Depth == 0)
	{
		if (Event.Package)
		{
			return Event.Package->Name;
		}
	}

	if (Event.Export && Event.Export->Class)
	{
		return Event.Export->Class->Name;
	}

	return TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return GetEventNameDelegate.Execute(Depth, Event);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::SetColorSchema(int32 Schema)
{
	switch (Schema)
	{
		case 0: GetEventNameDelegate = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByEventType); break;
		case 1: GetEventNameDelegate = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByPackageName); break;
		case 2: GetEventNameDelegate = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByExportClassName); break;
		case 3: GetEventNameDelegate = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByPackageAndExportClassName); break;
	};

	for (const auto& KV : LoadingTracks)
	{
		FLoadingTimingTrack& Track = *KV.Value;
		Track.SetDirtyFlag();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FLoadingTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		LoadTimeProfilerProvider.ReadTimeline(TimelineIndex, [this, &Builder, &Viewport](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
		{
			if (FTimingEventsTrack::bUseDownSampling)
			{
				const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();
				Timeline.EnumerateEventsDownSampled(Viewport.GetStartTime(), Viewport.GetEndTime(), SecondsPerPixel, [this, &Builder](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
				{
					const TCHAR* Name = SharedState.GetEventName(Depth, Event);
					const uint64 Type = static_cast<uint64>(Event.EventType);
					const uint32 Color = 0;
					Builder.AddEvent(StartTime, EndTime, Depth, Name, Type, Color);
					return Trace::EEventEnumerate::Continue;
				});
			}
			else
			{
				Timeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(), [this, &Builder](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
				{
					const TCHAR* Name = SharedState.GetEventName(Depth, Event);
					const uint64 Type = static_cast<uint64>(Event.EventType);
					const uint32 Color = 0;
					Builder.AddEvent(StartTime, EndTime, Depth, Name, Type, Color);
					return Trace::EEventEnumerate::Continue;
				});
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && InTooltipEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TooltipEvent = InTooltipEvent.As<FTimingEvent>();

		auto MatchEvent = [&TooltipEvent](double InStartTime, double InEndTime, uint32 InDepth)
		{
			return InDepth == TooltipEvent.GetDepth()
				&& InStartTime == TooltipEvent.GetStartTime()
				&& InEndTime == TooltipEvent.GetEndTime();
		};

		FTimingEventSearchParameters SearchParameters(TooltipEvent.GetStartTime(), TooltipEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
		FindLoadTimeProfilerCpuEvent(SearchParameters, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FLoadTimeProfilerCpuEvent& InFoundEvent)
		{
			InOutTooltip.ResetContent();

			InOutTooltip.AddTitle(SharedState.GetEventName(TooltipEvent.GetDepth(), InFoundEvent));

			const Trace::FPackageInfo* Package = InFoundEvent.Package;
			const Trace::FPackageExportInfo* Export = InFoundEvent.Export;

			InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetDuration()));
			InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), TooltipEvent.GetDepth()));

			if (Package)
			{
				InOutTooltip.AddNameValueTextLine(TEXT("Package Name:"), Package->Name);
				InOutTooltip.AddNameValueTextLine(TEXT("Header Size:"), FString::Printf(TEXT("%s bytes"), *FText::AsNumber(Package->Summary.TotalHeaderSize).ToString()));
				InOutTooltip.AddNameValueTextLine(TEXT("Package Summary:"), FString::Printf(TEXT("%d imports, %d exports"), Package->Summary.ImportCount, Package->Summary.ExportCount));
			}

			InOutTooltip.AddNameValueTextLine(TEXT("Export Event:"), FString::Printf(TEXT("%s"), Trace::GetLoadTimeProfilerObjectEventTypeString(InFoundEvent.EventType)));

			if (Export)
			{
				InOutTooltip.AddNameValueTextLine(TEXT("Export Class:"), Export->Class ? Export->Class->Name : TEXT("N/A"));
				InOutTooltip.AddNameValueTextLine(TEXT("Serial Size:"), FString::Printf(TEXT("%s bytes"), *FText::AsNumber(Export->SerialSize).ToString()));
			}

			InOutTooltip.UpdateLayout();
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FLoadingTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindLoadTimeProfilerCpuEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FLoadTimeProfilerCpuEvent& InFoundEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FLoadingTimingTrack::FindLoadTimeProfilerCpuEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FLoadTimeProfilerCpuEvent&)> InFoundPredicate) const
{
	return TTimingEventSearch<Trace::FLoadTimeProfilerCpuEvent>::Search(
		InParameters,

		[this](TTimingEventSearch<Trace::FLoadTimeProfilerCpuEvent>::FContext& InContext)
		{
			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

				if (Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
				{
					const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

					LoadTimeProfilerProvider.ReadTimeline(TimelineIndex, [&InContext](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
					{
						Timeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FLoadTimeProfilerCpuEvent& Event)
						{
							InContext.Check(EventStartTime, EventEndTime, EventDepth, Event);
							return InContext.ShouldContinueSearching() ? Trace::EEventEnumerate::Continue : Trace::EEventEnumerate::Stop;
						});
					});
				}
			}
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FLoadTimeProfilerCpuEvent& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
