// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FileActivityTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

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

void FFileActivitySharedState::Reset()
{
	bForceIoEventsUpdate = false;

	bMergeIoLanes = true;
	bShowFileActivityBackgroundEvents = false;

	FileActivities.Reset();
	FileActivityMap.Reset();

	AllIoEvents.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::Update()
{
	if (bForceIoEventsUpdate)
	{
		bForceIoEventsUpdate = false;

		FileActivities.Reset();
		FileActivityMap.Reset();
		AllIoEvents.Reset();

		FStopwatch Stopwatch;
		Stopwatch.Start();

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadFileActivityProvider(*Session.Get()))
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::IFileActivityProvider& FileActivityProvider = *Trace::ReadFileActivityProvider(*Session.Get());

			// Enumerate all IO events and cache them.
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
						Activity = MakeShareable(new FIoFileActivity());

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
// FFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivityTimingTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	Tooltip.ResetContent();

	const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(HoveredTimingEvent.TypeId & 0x0F);
	const bool bHasFailed = ((HoveredTimingEvent.TypeId & 0xF0) != 0);

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
	Tooltip.AddTitle(TypeStr, TypeLinearColor);

	Tooltip.AddTitle(HoveredTimingEvent.Path);

	Tooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.Duration()));
	Tooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), HoveredTimingEvent.Depth));

	if (ActivityType == Trace::FileActivityType_Read || ActivityType == Trace::FileActivityType_Write)
	{
		Tooltip.AddNameValueTextLine(TEXT("Offset:"), FText::AsNumber(HoveredTimingEvent.Offset).ToString() + TEXT(" bytes"));
		Tooltip.AddNameValueTextLine(TEXT("Size:"), FText::AsNumber(HoveredTimingEvent.Size).ToString() + TEXT(" bytes"));
	}

	Tooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivityTimingTrack::SearchEvent(const double InStartTime,
										   const double InEndTime,
										   TFunctionRef<bool(double, double, uint32)> InPredicate,
										   FTimingEvent& InOutTimingEvent,
										   bool bInStopAtFirstMatch,
										   bool bInSearchForLargestEvent,
										   bool bIgnoreEventDepth) const
{
	struct FSearchTimingEventContext
	{
		const double StartTime;
		const double EndTime;
		TFunctionRef<bool(double, double, uint32)> Predicate;
		FTimingEvent& TimingEvent;
		const bool bStopAtFirstMatch;
		const bool bSearchForLargestEvent;
		mutable bool bFound;
		mutable bool bContinueSearching;
		mutable double LargestDuration;

		FSearchTimingEventContext(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent)
			: StartTime(InStartTime)
			, EndTime(InEndTime)
			, Predicate(InPredicate)
			, TimingEvent(InOutTimingEvent)
			, bStopAtFirstMatch(bInStopAtFirstMatch)
			, bSearchForLargestEvent(bInSearchForLargestEvent)
			, bFound(false)
			, bContinueSearching(true)
			, LargestDuration(-1.0)
		{
		}

		void CheckEvent(const FFileActivitySharedState::FIoTimingEvent& Event, uint32 EventDepth)
		{
			if (bContinueSearching && Predicate(Event.StartTime, Event.EndTime, EventDepth))
			{
				if (!bSearchForLargestEvent || Event.EndTime - Event.StartTime > LargestDuration)
				{
					LargestDuration = Event.EndTime - Event.StartTime;

					TimingEvent.TypeId = Event.Type;
					TimingEvent.Depth = EventDepth;
					TimingEvent.StartTime = Event.StartTime;
					TimingEvent.EndTime = Event.EndTime;

					TimingEvent.Offset = Event.Offset;
					TimingEvent.Size = Event.Size;
					TimingEvent.Path = Event.FileActivity->Path;

					bFound = true;
					bContinueSearching = !bStopAtFirstMatch || bSearchForLargestEvent;
				}
			}
		}
	};

	FSearchTimingEventContext Ctx(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const FTimingEventsTrack* Track = Ctx.TimingEvent.Track;

		for (const FFileActivitySharedState::FIoTimingEvent& Event : State->GetAllEvents())
		{
			if (Event.EndTime <= Ctx.StartTime)
			{
				continue;
			}

			if (!Ctx.bContinueSearching || Event.StartTime >= Ctx.EndTime)
			{
				break;
			}

			Ctx.CheckEvent(Event, bIgnoreEventDepth ? 0 : Event.Depth);
		}
	}

	return Ctx.bFound;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FOverviewFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FOverviewFileActivityTimingTrack::Draw(FTimingViewDrawHelper& Helper) const
{
	FTimingEventsTrack& Track = *const_cast<FOverviewFileActivityTimingTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		// Draw the IO Overiew track using cached events (as those are sorted by Start Time).
		for (const FFileActivitySharedState::FIoTimingEvent& Event : State->AllIoEvents)
		{
			const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);

			if (ActivityType >= Trace::FileActivityType_Count)
			{
				// Ignore "Idle" and "NotClosed" events.
				continue;
			}

			//const double EventEndTime = Event.EndTime; // keep duration of events
			const double EventEndTime = Event.StartTime; // make all 0 duration events

			if (EventEndTime <= Helper.GetViewport().GetStartTime())
			{
				continue;
			}
			if (Event.StartTime >= Helper.GetViewport().GetEndTime())
			{
				break;
			}

			uint32 Color = GetFileActivityTypeColor(ActivityType);

			const bool bHasFailed = ((Event.Type & 0xF0) != 0);

			if (bHasFailed)
			{
				FString EventName = TEXT("Failed ");
				EventName += GetFileActivityTypeName(ActivityType);
				Color = 0xFFAA0000;
				Helper.AddEvent(Event.StartTime, EventEndTime, 0, *EventName, Color);
			}
			else
			{
				Helper.AddEvent(Event.StartTime, EventEndTime, 0, GetFileActivityTypeName(ActivityType), Color);
			}
		}

		Helper.EndTimeline(Track);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FDetailedFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FDetailedFileActivityTimingTrack::Draw(FTimingViewDrawHelper& Helper) const
{
	FTimingEventsTrack& Track = *const_cast<FDetailedFileActivityTimingTrack*>(this);

	// Draw IO track using cached events.
	if (Helper.BeginTimeline(Track))
	{
		// Draw IO file activity background events.
		if (State->bShowFileActivityBackgroundEvents)
		{
			for (const TSharedPtr<FFileActivitySharedState::FIoFileActivity> Activity : State->FileActivities)
			{
				if (Activity->EndTime <= Helper.GetViewport().GetStartTime())
				{
					continue;
				}
				if (Activity->StartTime >= Helper.GetViewport().GetEndTime())
				{
					break;
				}

				ensure(Activity->Depth <= 10000);

				Helper.AddEvent(Activity->StartTime, Activity->EndTime, Activity->Depth, Activity->Path, 0x55333333);
			}
		}

		// Draw IO file activity foreground events.
		for (const FFileActivitySharedState::FIoTimingEvent& Event : State->AllIoEvents)
		{
			if (Event.EndTime <= Helper.GetViewport().GetStartTime())
			{
				continue;
			}
			if (Event.StartTime >= Helper.GetViewport().GetEndTime())
			{
				break;
			}

			ensure(Event.Depth <= 10000);

			const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);

			uint32 Color = GetFileActivityTypeColor(ActivityType);

			if (ActivityType < Trace::FileActivityType_Count)
			{
				const bool bHasFailed = ((Event.Type & 0xF0) != 0);

				if (bHasFailed)
				{
					FString EventName = TEXT("Failed ");
					EventName += GetFileActivityTypeName(ActivityType);
					Color = 0xFFAA0000;
					Helper.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, *EventName, Color);
				}
				else
				{
					Helper.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, GetFileActivityTypeName(ActivityType), Color);
				}
			}
			else
			{
				FString EventName = GetFileActivityTypeName(ActivityType);
				EventName += " [";
				EventName += Event.FileActivity->Path;
				EventName += "]";
				Helper.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, *EventName, Color);
			}
		}

		Helper.EndTimeline(Track);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
