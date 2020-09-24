// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileActivityTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"

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
		IoOverviewTrack->SetOrder(FTimingTrackOrder::First);
		IoOverviewTrack->SetVisibilityFlag(bShowHideAllIoTracks);
		InSession.AddScrollableTrack(IoOverviewTrack);
	}

	if (!IoActivityTrack.IsValid())
	{
		IoActivityTrack = MakeShared<FDetailedFileActivityTimingTrack>(*this);
		IoActivityTrack->SetOrder(FTimingTrackOrder::Last);
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
					//if (EventEndTime == std::numeric_limits<double>::infinity())
					//{
					//	EventEndTime = EventStartTime;
					//}

					TSharedPtr<FIoFileActivity> Activity = FileActivityMap.FindRef(FileInfo.Id);
					if (!Activity.IsValid())
					{
						Activity = MakeShared<FIoFileActivity>();

						Activity->Id = FileInfo.Id;
						Activity->Path = FileInfo.Path;
						Activity->StartTime = EventStartTime;
						Activity->EndTime = EventEndTime;
						Activity->CloseStartTime = EventStartTime;
						Activity->CloseEndTime = EventEndTime;
						Activity->EventCount = 1;
						Activity->Depth = -1;

						FileActivities.Add(Activity);
						FileActivityMap.Add(FileInfo.Id, Activity);
					}
					else
					{
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
						else
						{
							// The time range for the Close event is stored separated;
							// for the purpose of avoiding lane collisions (overlaps) between activities.
							Activity->CloseStartTime = EventStartTime;
							Activity->CloseEndTime = EventEndTime;
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

					AllIoEvents.Add(FIoTimingEvent{ EventStartTime, EventEndTime, EventDepth, Type, FileActivity->Offset, FileActivity->Size, FileActivity->ActualSize, Activity });
					return Trace::EEventEnumerate::Continue;
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

					if (FileActivity->StartTime >= Lane->EndTime &&
						(FileActivity->StartTime >= Lane->CloseEndTime || FileActivity->EndTime <= Lane->CloseStartTime)) // avoids overlaps with Close event
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

	//InOutMenuBuilder.BeginSection("File Activity", LOCTEXT("FileActivityHeading", "File Activity"));
	//{
	//	InOutMenuBuilder.AddMenuEntry(
	//		LOCTEXT("ShowAllIoTracks", "I/O Tracks - I"),
	//		LOCTEXT("ShowAllIoTracks_Tooltip", "Show/hide the I/O (File Activity) tracks"),
	//		FSlateIcon(),
	//		FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ShowHideAllIoTracks),
	//				  FCanExecuteAction(),
	//				  FIsActionChecked::CreateSP(this, &FFileActivitySharedState::IsAllIoTracksToggleOn)),
	//		NAME_None, //"QuickFilterSeparator",
	//		EUserInterfaceActionType::ToggleButton
	//	);
	//}
	//InOutMenuBuilder.EndSection();

	InOutMenuBuilder.BeginSection("File Activity");
	{
		InOutMenuBuilder.AddSubMenu(
			LOCTEXT("FileActivity_SubMenu", "File Activity"),
			LOCTEXT("FileActivity_SubMenu_Desc", "File Activity track options"),
			FNewMenuDelegate::CreateSP(this, &FFileActivitySharedState::BuildSubMenu),
			false,
			FSlateIcon()
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::BuildSubMenu(FMenuBuilder& InOutMenuBuilder)
{
	InOutMenuBuilder.BeginSection("File Activity", LOCTEXT("FileActivityHeading", "File Activity"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowIoOverviewTrack", "I/O Overview Track - I"),
			LOCTEXT("ShowIoOverviewTrack_Tooltip", "Show/hide the I/O Overview track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ShowHideIoOverviewTrack),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FFileActivitySharedState::IsIoOverviewTrackVisible)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);

		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowOnlyErrors", "Show Only Errors (I/O Overview Track)"),
			LOCTEXT("ShowOnlyErrors_Tooltip", "Show only the events with errors, in the I/O Overview track."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ToggleOnlyErrors),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FFileActivitySharedState::IsOnlyErrorsToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);

		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowIoActivityTrack", "I/O Activity Track - I"),
			LOCTEXT("ShowIoActivityTrack_Tooltip", "Show/hide the I/O Activity track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ShowHideIoActivityTrack),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FFileActivitySharedState::IsIoActivityTrackVisible)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);

		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowBackgroundEvents", "Show Background Events (I/O Activity Track) - O"),
			LOCTEXT("ShowBackgroundEvents_Tooltip", "Show background events for file activities, in the I/O Activity track."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FFileActivitySharedState::ToggleBackgroundEvents),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FFileActivitySharedState::AreBackgroundEventsVisible)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::SetAllIoTracksToggle(bool bOnOff)
{
	bShowHideAllIoTracks = bOnOff;

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

bool FFileActivitySharedState::IsIoOverviewTrackVisible() const
{
	return IoOverviewTrack && IoOverviewTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ShowHideIoOverviewTrack()
{
	if (IoOverviewTrack.IsValid())
	{
		IoOverviewTrack->ToggleVisibility();
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}

	const bool bIsOverviewTrackVisible = IsIoOverviewTrackVisible();
	const bool bIsActivityTrackVisible = IsIoActivityTrackVisible();

	if (bIsOverviewTrackVisible == bIsActivityTrackVisible)
	{
		bShowHideAllIoTracks = bIsOverviewTrackVisible;
	}

	if (bIsOverviewTrackVisible)
	{
		RequestUpdate();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivitySharedState::IsIoActivityTrackVisible() const
{
	return IoActivityTrack && IoActivityTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ShowHideIoActivityTrack()
{
	if (IoActivityTrack.IsValid())
	{
		IoActivityTrack->ToggleVisibility();
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}

	const bool bIsOverviewTrackVisible = IsIoOverviewTrackVisible();
	const bool bIsActivityTrackVisible = IsIoActivityTrackVisible();

	if (bIsOverviewTrackVisible == bIsActivityTrackVisible)
	{
		bShowHideAllIoTracks = bIsOverviewTrackVisible;
	}

	if (bIsActivityTrackVisible)
	{
		RequestUpdate();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivitySharedState::IsOnlyErrorsToggleOn() const
{
	return IoOverviewTrack && IoOverviewTrack->IsOnlyErrorsToggleOn();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ToggleOnlyErrors()
{
	if (IoOverviewTrack)
	{
		IoOverviewTrack->ToggleOnlyErrors();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivitySharedState::AreBackgroundEventsVisible() const
{
	return IoActivityTrack && IoActivityTrack->AreBackgroundEventsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivitySharedState::ToggleBackgroundEvents()
{
	if (IoActivityTrack)
	{
		IoActivityTrack->ToggleBackgroundEvents();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FFileActivityTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFileActivityTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

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
		FindIoTimingEvent(SearchParameters, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
		{
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
			if (InEvent.ActualSize != InEvent.Size)
			{
				TypeStr += TEXT(" [!]");
			}
			FLinearColor TypeLinearColor = FLinearColor(FColor(TypeColor));
			TypeLinearColor.R *= 2.0f;
			TypeLinearColor.G *= 2.0f;
			TypeLinearColor.B *= 2.0f;
			InOutTooltip.AddTitle(TypeStr, TypeLinearColor);

			InOutTooltip.AddTitle(InEvent.FileActivity->Path);

			const double Duration = InEvent.EndTime - InEvent.StartTime;
			InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(Duration));

			if (ActivityType == Trace::FileActivityType_Read || ActivityType == Trace::FileActivityType_Write)
			{
				InOutTooltip.AddNameValueTextLine(TEXT("Offset:"), FText::AsNumber(InEvent.Offset).ToString() + TEXT(" bytes"));
				InOutTooltip.AddNameValueTextLine(TEXT("Size:"), FText::AsNumber(InEvent.Size).ToString() + TEXT(" bytes"));
				FString ActualSizeStr = FText::AsNumber(InEvent.ActualSize).ToString() + TEXT(" bytes");
				if (InEvent.ActualSize != InEvent.Size)
				{
					ActualSizeStr += TEXT(" [!]");
				}
				InOutTooltip.AddNameValueTextLine(TEXT("Actual Size:"), ActualSizeStr);
			}

			if (!bIgnoreEventDepth)
			{
				InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), InEvent.Depth));
			}

			InOutTooltip.UpdateLayout();
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFileActivityTimingTrack::FindIoTimingEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FFileActivitySharedState::FIoTimingEvent&)> InFoundPredicate) const
{
	return TTimingEventSearch<FFileActivitySharedState::FIoTimingEvent>::Search(
		InParameters,

		// Search...
		[this](TTimingEventSearch<FFileActivitySharedState::FIoTimingEvent>::FContext& InContext)
		{
			if (bIgnoreDuration)
			{
				for (const FFileActivitySharedState::FIoTimingEvent& Event : SharedState.GetAllEvents())
				{
					if (bShowOnlyErrors && ((Event.Type & 0xF0) == 0))
					{
						continue;
					}

					if (Event.StartTime < InContext.GetParameters().StartTime)
					{
						continue;
					}

					if (!InContext.ShouldContinueSearching() || Event.StartTime > InContext.GetParameters().EndTime)
					{
						break;
					}

					InContext.Check(Event.StartTime, Event.StartTime, bIgnoreEventDepth ? 0 : Event.Depth, Event);
				}
			}
			else
			{
				for (const FFileActivitySharedState::FIoTimingEvent& Event : SharedState.GetAllEvents())
				{
					if (bShowOnlyErrors && ((Event.Type & 0xF0) == 0))
					{
						continue;
					}

					if (!bIgnoreDuration && Event.EndTime <= InContext.GetParameters().StartTime)
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

		if (bShowOnlyErrors && !bHasFailed)
		{
			continue;
		}

		uint32 Color = bHasFailed ? 0xFFAA0000 : GetFileActivityTypeColor(ActivityType);
		if (Event.ActualSize != Event.Size)
		{
			Color = (Color & 0xFF000000) | ((Color & 0xFEFEFE) >> 1);
		}

		Builder.AddEvent(Event.StartTime, EventEndTime, 0, Color,
			[&Event](float Width)
			{
				FString EventName;

				const bool bHasFailed = ((Event.Type & 0xF0) != 0);
				if (bHasFailed)
				{
					EventName += TEXT("Failed ");
				}

				const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);
				EventName += GetFileActivityTypeName(ActivityType);

				if (Event.ActualSize != Event.Size)
				{
					EventName += TEXT(" [!]");
				}

				if (Width > EventName.Len() * 4.0f + 32.0f)
				{
					const double Duration = Event.EndTime - Event.StartTime; // actual event duration
					FTimingEventsTrackDrawStateBuilder::AppendDurationToEventName(EventName, Duration);
				}

				return EventName;
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FOverviewFileActivityTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindIoTimingEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FOverviewFileActivityTimingTrack::BuildContextMenu(FMenuBuilder& InOutMenuBuilder)
{
	InOutMenuBuilder.BeginSection(TEXT("Misc"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("OverviewTrack_ShowOnlyErrors", "Show Only Errors"),
			LOCTEXT("OverviewTrack_ShowOnlyErrors_Tooltip", "Show only the events with errors"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FOverviewFileActivityTimingTrack::ToggleOnlyErrors),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FOverviewFileActivityTimingTrack::IsOnlyErrorsToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FDetailedFileActivityTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FDetailedFileActivityTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	// Add IO file activity background events.
	if (bShowBackgroundEvents)
	{
		for (const TSharedPtr<FFileActivitySharedState::FIoFileActivity>& Activity : SharedState.FileActivities)
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

			Builder.AddEvent(Activity->StartTime, Activity->EndTime, Activity->Depth, 0x55333333,
				[&Activity](float Width)
				{
					FString EventName = Activity->Path;

					if (Width > EventName.Len() * 4.0f + 32.0f)
					{
						const double Duration = Activity->EndTime - Activity->StartTime;
						FTimingEventsTrackDrawStateBuilder::AppendDurationToEventName(EventName, Duration);
					}

					return EventName;
				});
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

		const bool bHasFailed = ((Event.Type & 0xF0) != 0);

		if (bShowOnlyErrors && !bHasFailed)
		{
			continue;
		}

		uint32 Color = bHasFailed ? 0xFFAA0000 : GetFileActivityTypeColor(ActivityType);
		if (Event.ActualSize != Event.Size)
		{
			Color = (Color & 0xFF000000) | ((Color & 0xFEFEFE) >> 1);
		}

		Builder.AddEvent(Event.StartTime, Event.EndTime, Event.Depth, Color,
			[&Event](float Width)
			{
				FString EventName;

				const bool bHasFailed = ((Event.Type & 0xF0) != 0);
				if (bHasFailed)
				{
					EventName += TEXT("Failed ");
				}

				const Trace::EFileActivityType ActivityType = static_cast<Trace::EFileActivityType>(Event.Type & 0x0F);
				EventName += GetFileActivityTypeName(ActivityType);

				if (Event.ActualSize != Event.Size)
				{
					EventName += TEXT(" [!]");
				}

				if (ActivityType >= Trace::FileActivityType_Count)
				{
					EventName += " [";
					EventName += Event.FileActivity->Path;
					EventName += "]";
				}

				if (Width > EventName.Len() * 4.0f + 32.0f)
				{
					const double Duration = Event.EndTime - Event.StartTime;
					FTimingEventsTrackDrawStateBuilder::AppendDurationToEventName(EventName, Duration);
				}

				return EventName;
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FDetailedFileActivityTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindIoTimingEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FFileActivitySharedState::FIoTimingEvent& InEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FDetailedFileActivityTimingTrack::BuildContextMenu(FMenuBuilder& InOutMenuBuilder)
{
	InOutMenuBuilder.BeginSection(TEXT("Misc"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ActivityTrack_ShowOnlyErrors", "Show Only Errors"),
			LOCTEXT("ActivityTrack_ShowOnlyErrors_Tooltip", "Show only the events with errors"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FDetailedFileActivityTimingTrack::ToggleOnlyErrors),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FDetailedFileActivityTimingTrack::IsOnlyErrorsToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);

		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ActivityTrack_ShowBackgroundEvents", "Show Background Events - O"),
			LOCTEXT("ActivityTrack_ShowBackgroundEvents_Tooltip", "Show background events for file activities."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FDetailedFileActivityTimingTrack::ToggleBackgroundEvents),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FDetailedFileActivityTimingTrack::AreBackgroundEventsVisible)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
