// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileActivityTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "FileActivityTimingTrack"

// The FileActivity (I/O) timelines are just prototypes for now.
// Below code will be removed once the functionality is moved in analyzer.

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* GetFileActivityTypeName(Trace::EFileActivityType Type)
{
	static_assert(Trace::FileActivityType_Open == 0, "Trace::EFileActivityType enum has changed!?");
	static_assert(Trace::FileActivityType_Close == 1, "Trace::EFileActivityType enum has changed!?");
	static_assert(Trace::FileActivityType_Read == 2, "Trace::EFileActivityType enum has changed!?");
	static_assert(Trace::FileActivityType_Write == 3, "Trace::EFileActivityType enum has changed!?");
	static_assert(Trace::FileActivityType_Count == 4, "Trace::EFileActivityType enum has changed!?");
	static const TCHAR* GFileActivityTypeNames[] =
	{
		TEXT("Open"),
		TEXT("Close"),
		TEXT("Read"),
		TEXT("Write"),
		TEXT("Idle"), // virtual events added for cases where Close event is more than 1s away from last Open/Read/Write event.
		TEXT("NotClosed") // virtual events added when an Open activity never closes
	};
	return GFileActivityTypeNames[Type];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 GetFileActivityTypeColor(Trace::EFileActivityType Type)
{
	static const uint32 GFileActivityTypeColors[] =
	{
		0xFFCCAA33, // open
		0xFF33AACC, // close
		0xFF33AA33, // read
		0xFFDD33CC, // write
		0x55333333, // idle
		0x55553333, // close
	};
	return GFileActivityTypeColors[Type];
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFileActivitySharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	IoOverviewTrack.Reset();
	IoActivityTrack.Reset();

	bShowHideAllIoTracks = false;
	bForceIoEventsUpdate = false;
	bMergeIoLanes = true;
	bShowFileActivityBackgroundEvents = false;

	FileActivities.Reset();
	FileActivityMap.Reset();
	AllIoEvents.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	IoOverviewTrack.Reset();
	IoActivityTrack.Reset();

	bShowHideAllIoTracks = false;
	bForceIoEventsUpdate = false;
	bMergeIoLanes = true;
	bShowFileActivityBackgroundEvents = false;

	FileActivities.Reset();
	FileActivityMap.Reset();
	AllIoEvents.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (!Trace::ReadFileActivityProvider(InAnalysisSession))
	{
		return;
	}

	if (!IoOverviewTrack.IsValid())
	{
		IoOverviewTrack = MakeShared<FOverviewFileActivityTimingTrack>(*this);
		IoOverviewTrack->SetOrder(-999999); // first track
		IoOverviewTrack->SetVisibilityFlag(bShowHideAllIoTracks);
		InSession.AddScrollableTrack(IoOverviewTrack);
	}

	if (!IoActivityTrack.IsValid())
	{
		IoActivityTrack = MakeShared<FDetailedFileActivityTimingTrack>(*this);
		IoActivityTrack->SetOrder(+999999); // last track
		IoActivityTrack->SetVisibilityFlag(bShowHideAllIoTracks);
		InSession.AddScrollableTrack(IoActivityTrack);
	}

	if (bForceIoEventsUpdate)
	{
		bForceIoEventsUpdate = false;

		FileActivities.Reset();
		FileActivityMap.Reset();
		AllIoEvents.Reset();

		FStopwatch Stopwatch;
		Stopwatch.Start();

		// Enumerate all IO events and cache them.
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);
			const Trace::IFileActivityProvider& FileActivityProvider = *Trace::ReadFileActivityProvider(InAnalysisSession);
			FileActivityProvider.EnumerateFileActivity([this](const Trace::FFileInfo& FileInfo, const Trace::IFileActivityProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEvents(-std::numeric_limits<double>::infinity(), +std::numeric_limits<double>::infinity(),
					[this, &FileInfo, &Timeline](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FFileActivity* FileActivity)
				{
					TSharedPtr<FIoFileActivity> Activity;

					//if (EventEndTime == std::numeric_limits<double>::infinity())
					//{
					//	EventEndTime = EventStartTime;
					//}

					if (!FileActivityMap.Contains(FileInfo.Id))
					{
						Activity = MakeShared<FIoFileActivity>();

						Activity->Id = FileInfo.Id;
						Activity->Path = FileInfo.Path;
						Activity->StartTime = EventStartTime;
						Activity->EndTime = EventEndTime;
						Activity->EventCount = 1;
						Activity->Depth = -1;

						FileActivities.Add(Activity);
						FileActivityMap.Add(FileInfo.Id, Activity);
					}
					else
					{
						Activity = FileActivityMap[FileInfo.Id];

						if (FileActivity->ActivityType != Trace::FileActivityType_Close)
						{
							ensure(EventStartTime >= Activity->StartTime);
							if (EventStartTime < Activity->StartTime)
							{
								Activity->StartTime = EventStartTime;
							}

							if (EventEndTime > Activity->EndTime)
							{
								Activity->EndTime = EventEndTime;
							}
						}

						Activity->EventCount++;
					}

					if (bMergeIoLanes)
					{
						EventDepth = 0;
					}
					else
					{
						EventDepth = FileInfo.Id % 32; // simple layout
					}

					uint32 Type = ((uint32)FileActivity->ActivityType & 0x0F) | (FileActivity->Failed ? 0x80 : 0);

					AllIoEvents.Add(FIoTimingEvent{ EventStartTime, EventEndTime, EventDepth, Type, FileActivity->Offset, FileActivity->Size, Activity });
				});

				return true;
			});
		}

		Stopwatch.Stop();
		UE_LOG(TimingProfiler, Log, TEXT("[IO] Enumerated %s events (%s file activities) in %s."),
			*FText::AsNumber(AllIoEvents.Num()).ToString(),
			*FText::AsNumber(FileActivities.Num()).ToString(),
			*TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));
		Stopwatch.Restart();

		// Sort cached IO file activities by Start Time.
		FileActivities.Sort([](const TSharedPtr<FIoFileActivity>& A, const TSharedPtr<FIoFileActivity>& B) { return A->StartTime < B->StartTime; });

		// Sort cached IO events by Start Time.
		AllIoEvents.Sort([](const FIoTimingEvent& A, const FIoTimingEvent& B) { return A.StartTime < B.StartTime; });

		Stopwatch.Stop();
		UE_LOG(TimingProfiler, Log, TEXT("[IO] Sorted file activities and events in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));

		if (bMergeIoLanes)
		{
			//////////////////////////////////////////////////
			// Compute depth for file activities (avoids overlaps).

			Stopwatch.Restart();

			TArray<TSharedPtr<FIoFileActivity>> ActivityLanes;

			for (TSharedPtr<FIoFileActivity> FileActivity : FileActivities)
			{
				// Find lane (avoiding overlaps with other file activities).
				for (int32 LaneIndex = 0; LaneIndex < ActivityLanes.Num(); ++LaneIndex)
				{
					TSharedPtr<FIoFileActivity> Lane = ActivityLanes[LaneIndex];

					if (FileActivity->StartTime >= Lane->EndTime)
					{
						FileActivity->Depth = LaneIndex;
						ActivityLanes[LaneIndex] = FileActivity;
						break;
					}
				}

				if (FileActivity->Depth < 0)
				{
					const int32 MaxLanes = 10000;
					if (ActivityLanes.Num() < MaxLanes)
					{
						// Add new lane.
						FileActivity->Depth = ActivityLanes.Num();
						ActivityLanes.Add(FileActivity);
					}
					else
					{
						int32 LaneIndex = ActivityLanes.Num() - 1;
						FileActivity->Depth = LaneIndex;
						TSharedPtr<FIoFileActivity> Lane = ActivityLanes[LaneIndex];
						if (FileActivity->EndTime > Lane->EndTime)
						{
							ActivityLanes[LaneIndex] = FileActivity;
						}
					}
				}
			}

			Stopwatch.Stop();
			UE_LOG(TimingProfiler, Log, TEXT("[IO] Computed layout for file activities in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));

			//////////////////////////////////////////////////

			Stopwatch.Restart();

			for (FIoTimingEvent& Event : AllIoEvents)
			{
				Event.Depth = static_cast<uint32>(Event.FileActivity->Depth);
			}

			Stopwatch.Stop();
			UE_LOG(TimingProfiler, Log, TEXT("[IO] Updated depth for events in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));

			//////////////////////////////////////////////////
			/*

			Stopwatch.Restart();

			struct FIoLane
			{
				uint32 FileId;
				const TCHAR* Path;
				double LastEndTime;
				double EndTime;
			};

			TArray<FIoLane> Lanes;

			TArray<FIoTimingEvent> IoEventsToAdd;

			for (FIoTimingEvent& Event : AllIoEvents)
			{
				uint64 TimelineId = Event.Depth;

				int32 Depth = -1;

				bool bIsCloseEvent = false;

				const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);

				if (ActivityType == Trace::FileActivityType_Open)
				{
					// Find lane (avoiding overlaps with other opened files).
					for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
					{
						FIoLane& Lane = Lanes[LaneIndex];
						if (Event.StartTime >= Lane.EndTime)
						{
							Depth = LaneIndex;
							break;
						}
					}

					if (Depth < 0)
					{
						// Add new lane.
						Depth = Lanes.AddDefaulted();
					}

					const bool bHasFailed = ((Event.Type & 0xF0) != 0);

					FIoLane& Lane = Lanes[Depth];
					Lane.FileId = TimelineId;
					Lane.Path = Event.Path;
					Lane.LastEndTime = Event.EndTime;
					Lane.EndTime = bHasFailed ? Event.EndTime : std::numeric_limits<double>::infinity();
				}
				else if (ActivityType == Trace::FileActivityType_Close)
				{
					bIsCloseEvent = true;

					// Find lane with same id.
					for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
					{
						FIoLane& Lane = Lanes[LaneIndex];
						if (Lane.FileId == TimelineId)
						{
							// Adds an Idle event, but only if has passed at least 1s since last open/read/write activity.
							if (Event.StartTime - Lane.LastEndTime > 1.0)
							{
								IoEventsToAdd.Add(FIoTimingEvent{ Lane.LastEndTime, Event.StartTime, static_cast<uint32>(LaneIndex), Trace::FileActivityType_Count, Event.Path });
							}
							Lane.LastEndTime = Event.EndTime;
							Lane.EndTime = Event.EndTime;
							Depth = LaneIndex;
							break;
						}
					}
					ensure(Depth >= 0);
				}
				else
				{
					// All other events should be inside the virtual Open-Close event.
					// Find lane with same id.
					for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
					{
						FIoLane& Lane = Lanes[LaneIndex];
						if (Lane.FileId == TimelineId)
						{
							Lane.LastEndTime = Event.EndTime;
							Depth = LaneIndex;
							break;
						}
					}
					ensure(Depth >= 0);
				}

				if (Depth < 0) // just in case
				{
					// Add new lane.
					Depth = Lanes.AddDefaulted();
					FIoLane& Lane = Lanes[Depth];
					Lane.FileId = TimelineId;
					Lane.Path = Event.Path;
					Lane.LastEndTime = Event.EndTime;
					Lane.EndTime = bIsCloseEvent ? Event.EndTime : std::numeric_limits<double>::infinity();
				}

				Event.Depth = Depth;
			}

			Stopwatch.Stop();
			UE_LOG(TimingProfiler, Log, TEXT("[IO] Merge 1/3 in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));
			Stopwatch.Restart();

			for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
			{
				FIoLane& Lane = Lanes[LaneIndex];
				if (Lane.EndTime == std::numeric_limits<double>::infinity())
				{
					IoEventsToAdd.Add(FIoTimingEvent{ Lane.LastEndTime, Lane.EndTime, static_cast<uint32>(LaneIndex), Trace::FileActivityType_Count + 1, Lane.Path });
				}
			}

			for (const FIoTimingEvent& Event : IoEventsToAdd)
			{
				AllIoEvents.Add(Event);
			}

			Stopwatch.Stop();
			UE_LOG(TimingProfiler, Log, TEXT("[IO] Merge 2/3 in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));
			Stopwatch.Restart();

			// Sort cached IO events one more time, also by Start Time.
			AllIoEvents.Sort([](const FIoTimingEvent& A, const FIoTimingEvent& B) { return A.StartTime < B.StartTime; });

			//// Sort cached IO events again, by Depth and then by Start Time.
			//AllIoEvents.Sort([](const FIoTimingEvent& A, const FIoTimingEvent& B)
			//{
			//	return A.Depth == B.Depth ? A.StartTime < B.StartTime : A.Depth < B.Depth;
			//});

			Stopwatch.Stop();
			UE_LOG(TimingProfiler, Log, TEXT("[IO] Merge 3/3 (sort) in %s."), *TimeUtils::FormatTimeAuto(Stopwatch.GetAccumulatedTime()));
			*/
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("File Activity", LOCTEXT("FileActivityHeading", "File Activity"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllIoTracks", "I/O Tracks - I"),
			LOCTEXT("ShowAllIoTracks_Tooltip", "Show/hide the I/O (File Activity) tracks"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ShowHideAllIoTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FFileActivitySharedState::ShowHideAllIoTracks_IsChecked)),
			"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivitySharedState::ShowHideAllIoTracks_IsChecked() const
{
	return bShowHideAllIoTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ShowHideAllIoTracks_Execute()
{
	bShowHideAllIoTracks = !bShowHideAllIoTracks;

	if (IoOverviewTrack.IsValid())
	{
		IoOverviewTrack->SetVisibilityFlag(bShowHideAllIoTracks);
	}
	if (IoActivityTrack.IsValid())
	{
		IoActivityTrack->SetVisibilityFlag(bShowHideAllIoTracks);
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}

	if (bShowHideAllIoTracks)
	{
		RequestUpdate();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ToggleBackgroundEvents()
{
	bShowFileActivityBackgroundEvents = !bShowFileActivityBackgroundEvents;

	if (IoActivityTrack.IsValid())
	{
		IoActivityTrack->SetDirtyFlag();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivityTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InTooltipEvent))
	{
		const FTimingEvent& TooltipEvent = static_cast<const FTimingEvent&>(InTooltipEvent);

		auto MatchEvent = [&TooltipEvent](double InStartTime, double InEndTime, uint32 InDepth)
		{
			return InDepth == TooltipEvent.GetDepth()
				&& InStartTime == TooltipEvent.GetStartTime()
				&& InEndTime == TooltipEvent.GetEndTime();
		};

		FTimingEventSearchParameters SearchParameters(TooltipEvent.GetStartTime(), TooltipEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
		FindIoTimingEvent(SearchParameters, false, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
		{
			InOutTooltip.ResetContent();

			const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(InEvent.Type & 0x0F);
			const bool bHasFailed = ((InEvent.Type & 0xF0) != 0);

			FString TypeStr;
			uint32 TypeColor;
			if (bHasFailed)
			{
				TypeStr = TEXT("Failed ");
				TypeStr += GetFileActivityTypeName(ActivityType);
				TypeColor = 0xFFFF3333;
			}
			else
			{
				TypeStr = GetFileActivityTypeName(ActivityType);
				TypeColor = GetFileActivityTypeColor(ActivityType);
			}
			FLinearColor TypeLinearColor = FLinearColor(FColor(TypeColor));
			TypeLinearColor.R *= 2.0f;
			TypeLinearColor.G *= 2.0f;
			TypeLinearColor.B *= 2.0f;
			InOutTooltip.AddTitle(TypeStr, TypeLinearColor);

			InOutTooltip.AddTitle(InEvent.FileActivity->Path);

			InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetDuration()));
			InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), TooltipEvent.GetDepth()));

			if (ActivityType == Trace::FileActivityType_Read || ActivityType == Trace::FileActivityType_Write)
			{
				InOutTooltip.AddNameValueTextLine(TEXT("Offset:"), FText::AsNumber(InEvent.Offset).ToString() + TEXT(" bytes"));
				InOutTooltip.AddNameValueTextLine(TEXT("Size:"), FText::AsNumber(InEvent.Size).ToString() + TEXT(" bytes"));
			}

			InOutTooltip.UpdateLayout();
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivityTimingTrack::FindIoTimingEvent(const FTimingEventSearchParameters& InParameters, bool bIgnoreEventDepth, TFunctionRef<void(double, double, uint32, const FFileActivitySharedState::FIoTimingEvent&)> InFoundPredicate) const
{
	return TTimingEventSearch<FFileActivitySharedState::FIoTimingEvent>::Search(
		InParameters,

		// Search...
		[this, bIgnoreEventDepth](TTimingEventSearch<FFileActivitySharedState::FIoTimingEvent>::FContext& InContext)
		{
			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

				for (const FFileActivitySharedState::FIoTimingEvent& Event : SharedState.GetAllEvents())
				{
					if (Event.EndTime <= InContext.GetParameters().StartTime)
					{
						continue;
					}

					if (!InContext.ShouldContinueSearching() || Event.StartTime >= InContext.GetParameters().EndTime)
					{
						break;
					}

					InContext.Check(Event.StartTime, Event.EndTime, bIgnoreEventDepth ? 0 : Event.Depth, Event);
				}
			}
		},

		// Found!
		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FOverviewFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FOverviewFileActivityTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	for (const FFileActivitySharedState::FIoTimingEvent& Event : SharedState.AllIoEvents)
	{
		const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);
		const uint64 EventType = static_cast<uint64>(ActivityType);

		if (ActivityType >= Trace::FileActivityType_Count)
		{
			// Ignore "Idle" and "NotClosed" events.
			continue;
		}

		//const double EventEndTime = Event.EndTime; // keep duration of events
		const double EventEndTime = Event.StartTime; // make all 0 duration events

		if (EventEndTime <= Viewport.GetStartTime())
		{
			continue;
		}
		if (Event.StartTime >= Viewport.GetEndTime())
		{
			break;
		}

		const bool bHasFailed = ((Event.Type & 0xF0) != 0);

		if (bHasFailed)
		{
			FString EventName = TEXT("Failed ");
			EventName += GetFileActivityTypeName(ActivityType);
			const uint32 Color = 0xFFAA0000;
			Builder.AddEvent(Event.StartTime, EventEndTime, 0, *EventName, EventType, Color);
		}
		else
		{
			const uint32 Color = GetFileActivityTypeColor(ActivityType);
			Builder.AddEvent(Event.StartTime, EventEndTime, 0, GetFileActivityTypeName(ActivityType), EventType, Color);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FOverviewFileActivityTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	constexpr bool bIgnoreEventDepth = true;
	FindIoTimingEvent(InSearchParameters, bIgnoreEventDepth, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FDetailedFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FDetailedFileActivityTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	// Add IO file activity background events.
	if (SharedState.bShowFileActivityBackgroundEvents)
	{
		for (const TSharedPtr<FFileActivitySharedState::FIoFileActivity> Activity : SharedState.FileActivities)
		{
			if (Activity->EndTime <= Viewport.GetStartTime())
			{
				continue;
			}
			if (Activity->StartTime >= Viewport.GetEndTime())
			{
				break;
			}

			ensure(Activity->Depth <= 10000);

			const uint64 EventType = uint64(Trace::EFileActivityType::FileActivityType_Count);

			Builder.AddEvent(Activity->StartTime, Activity->EndTime, Activity->Depth, Activity->Path, EventType, 0x55333333);
		}
	}

	// Add IO file activity foreground events.
	for (const FFileActivitySharedState::FIoTimingEvent& Event : SharedState.AllIoEvents)
	{
		if (Event.EndTime <= Viewport.GetStartTime())
		{
			continue;
		}
		if (Event.StartTime >= Viewport.GetEndTime())
		{
			break;
		}

		ensure(Event.Depth <= 10000);

		const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);
		const uint64 EventType = static_cast<uint64>(ActivityType);

		if (ActivityType < Trace::FileActivityType_Count)
		{
			const bool bHasFailed = ((Event.Type & 0xF0) != 0);

			if (bHasFailed)
			{
				FString EventName = TEXT("Failed ");
				EventName += GetFileActivityTypeName(ActivityType);
				const uint32 Color = 0xFFAA0000;
				Builder.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, *EventName, EventType, Color);
			}
			else
			{
				const uint32 Color = GetFileActivityTypeColor(ActivityType);
				Builder.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, GetFileActivityTypeName(ActivityType), EventType, Color);
			}
		}
		else
		{
			FString EventName = GetFileActivityTypeName(ActivityType);
			EventName += " [";
			EventName += Event.FileActivity->Path;
			EventName += "]";
			const uint32 Color = GetFileActivityTypeColor(ActivityType);
			Builder.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, *EventName, EventType, Color);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FDetailedFileActivityTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	constexpr bool bIgnoreEventDepth = false;
	FindIoTimingEvent(InSearchParameters, bIgnoreEventDepth, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
