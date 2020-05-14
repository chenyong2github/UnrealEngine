// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/TimingEventsTrack.h"

#include "Insights/Common/Stopwatch.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"

#define LOCTEXT_NAMESPACE "TimingEventsTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTimingEventsTrack)

bool FTimingEventsTrack::bUseDownSampling = true;

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::FTimingEventsTrack()
	: FBaseTimingTrack()
	, NumLanes(0)
	, DrawState(MakeShared<FTimingEventsTrackDrawState>())
	, FilteredDrawState(MakeShared<FTimingEventsTrackDrawState>())
{
	SetValidLocations(ETimingTrackLocation::Scrollable | ETimingTrackLocation::TopDocked | ETimingTrackLocation::BottomDocked);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::FTimingEventsTrack(const FString& InName)
	: FBaseTimingTrack(InName)
	, NumLanes(0)
	, DrawState(MakeShared<FTimingEventsTrackDrawState>())
	, FilteredDrawState(MakeShared<FTimingEventsTrackDrawState>())
{
	SetValidLocations(ETimingTrackLocation::Scrollable | ETimingTrackLocation::TopDocked | ETimingTrackLocation::BottomDocked);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::~FTimingEventsTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::Reset()
{
	FBaseTimingTrack::Reset();

	NumLanes = 0;
	DrawState->Reset();
	FilteredDrawState->Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::PreUpdate(const ITimingTrackUpdateContext& Context)
{
	if (IsDirty() || Context.GetViewport().IsHorizontalViewportDirty())
	{
		ClearDirtyFlag();

		int32 MaxDepth = -1;

		{
			FTimingEventsTrackDrawStateBuilder Builder(*DrawState, Context.GetViewport());

			BuildDrawState(Builder, Context);

			Builder.Flush();

			if (Builder.GetMaxDepth() > MaxDepth)
			{
				MaxDepth = Builder.GetMaxDepth();
			}
		}

		const TSharedPtr<ITimingEventFilter> EventFilter = Context.GetEventFilter();
		if (EventFilter.IsValid() && EventFilter->FilterTrack(*this))
		{
			const FTimingTrackViewport& Viewport = Context.GetViewport();

			const bool bFastLastBuild = FilteredDrawStateInfo.LastBuildDuration < 0.005; // LastBuildDuration < 5ms
			const bool bFilterPointerChanged = !FilteredDrawStateInfo.LastEventFilter.HasSameObject(EventFilter.Get());
			const bool bFilterContentChanged = FilteredDrawStateInfo.LastFilterChangeNumber != EventFilter->GetChangeNumber();

			if (bFastLastBuild || bFilterPointerChanged || bFilterContentChanged)
			{
				FilteredDrawStateInfo.LastEventFilter = EventFilter;
				FilteredDrawStateInfo.LastFilterChangeNumber = EventFilter->GetChangeNumber();
				FilteredDrawStateInfo.ViewportStartTime = Context.GetViewport().GetStartTime();
				FilteredDrawStateInfo.ViewportScaleX = Context.GetViewport().GetScaleX();
				FilteredDrawStateInfo.Counter = 0;
			}
			else
			{
				if (FilteredDrawStateInfo.ViewportStartTime == Viewport.GetStartTime() &&
					FilteredDrawStateInfo.ViewportScaleX == Viewport.GetScaleX())
				{
					if (FilteredDrawStateInfo.Counter > 0)
					{
						FilteredDrawStateInfo.Counter--;
					}
				}
				else
				{
					FilteredDrawStateInfo.ViewportStartTime = Context.GetViewport().GetStartTime();
					FilteredDrawStateInfo.ViewportScaleX = Context.GetViewport().GetScaleX();
					FilteredDrawStateInfo.Counter = 1; // wait
				}
			}

			if (FilteredDrawStateInfo.Counter == 0)
			{
				FStopwatch Stopwatch;
				Stopwatch.Start();
				{
					FTimingEventsTrackDrawStateBuilder Builder(*FilteredDrawState, Context.GetViewport());
					BuildFilteredDrawState(Builder, Context);
					Builder.Flush();
				}
				Stopwatch.Stop();
				FilteredDrawStateInfo.LastBuildDuration = Stopwatch.GetAccumulatedTime();
			}
			else
			{
				FilteredDrawState->Reset();
				FilteredDrawStateInfo.Opacity = 0.0f;
				SetDirtyFlag();
			}
		}
		else
		{
			FilteredDrawStateInfo.LastBuildDuration = 0.0;

			if (FilteredDrawStateInfo.LastEventFilter.IsValid())
			{
				FilteredDrawStateInfo.LastEventFilter.Reset();
				FilteredDrawStateInfo.LastFilterChangeNumber = 0;
				FilteredDrawStateInfo.Counter = 0;
				FilteredDrawState->Reset();
			}
		}

		SetNumLanes(MaxDepth + 1);
	}

	UpdateTrackHeight(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::UpdateTrackHeight(const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	const float CurrentTrackHeight = GetHeight();
	const float DesiredTrackHeight = Viewport.GetLayout().ComputeTrackHeight(NumLanes);

	if (CurrentTrackHeight < DesiredTrackHeight)
	{
		float NewTrackHeight;
		if (Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
		{
			NewTrackHeight = DesiredTrackHeight;
		}
		else
		{
			NewTrackHeight = FMath::CeilToFloat(CurrentTrackHeight * 0.9f + DesiredTrackHeight * 0.1f);
		}
		SetHeight(NewTrackHeight);
	}
	else if (CurrentTrackHeight > DesiredTrackHeight)
	{
		float NewTrackHeight;
		if (Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
		{
			NewTrackHeight = DesiredTrackHeight;
		}
		else
		{
			NewTrackHeight = FMath::FloorToFloat(CurrentTrackHeight * 0.9f + DesiredTrackHeight * 0.1f);
		}
		SetHeight(NewTrackHeight);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::PostUpdate(const ITimingTrackUpdateContext& Context)
{
	FBaseTimingTrack::PostUpdate(Context);

	constexpr float HeaderWidth = 100.0f;
	constexpr float HeaderHeight = 14.0f;

	const float MouseY = Context.GetMousePosition().Y;
	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);

		const float MouseX = Context.GetMousePosition().X;
		SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);
	}
	else
	{
		SetHoveredState(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	DrawEvents(Context, 1.0f);
	DrawHeader(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::DrawEvents(const ITimingTrackDrawContext& Context, const float OffsetY) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());

	if (Context.GetEventFilter().IsValid())
	{
		Helper.DrawFadedEvents(*DrawState, *this, OffsetY, 0.1f);

		if (FilteredDrawStateInfo.Opacity == 1.0f)
		{
			Helper.DrawEvents(*FilteredDrawState, *this, OffsetY);
		}
		else
		{
			FilteredDrawStateInfo.Opacity = FMath::Min(1.0f, FilteredDrawStateInfo.Opacity + 0.05f);
			Helper.DrawFadedEvents(*FilteredDrawState, *this, OffsetY, FilteredDrawStateInfo.Opacity);
		}
	}
	else
	{
		Helper.DrawEvents(*DrawState, *this, OffsetY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::DrawMarkers(const ITimingTrackDrawContext& Context, float LineY, float LineH) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());

	if (Context.GetEventFilter().IsValid())
	{
		Helper.DrawMarkers(*DrawState, LineY, LineH, 0.2f);
		Helper.DrawMarkers(*FilteredDrawState, LineY, LineH, 0.75f * FilteredDrawStateInfo.Opacity);
	}
	else
	{
		Helper.DrawMarkers(*DrawState, LineY, LineH, 0.2f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingEventsTrack::GetHeaderBackgroundLayerId(const ITimingTrackDrawContext& Context) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
	return Helper.GetHeaderBackgroundLayerId();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTimingEventsTrack::GetHeaderTextLayerId(const ITimingTrackDrawContext& Context) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
	return Helper.GetHeaderTextLayerId();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::DrawHeader(const ITimingTrackDrawContext& Context) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
	Helper.DrawTrackHeader(*this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::DrawEvent(const ITimingTrackDrawContext& Context, const ITimingEvent& InTimingEvent, EDrawEventMode InDrawMode) const
{
	if (InTimingEvent.CheckTrack(this) && InTimingEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TrackEvent = InTimingEvent.As<FTimingEvent>();
		const FTimingViewLayout& Layout = Context.GetViewport().GetLayout();
		const float Y = TrackEvent.GetTrack()->GetPosY() + Layout.GetLaneY(TrackEvent.GetDepth());

		const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
		Helper.DrawTimingEventHighlight(TrackEvent.GetStartTime(), TrackEvent.GetEndTime(), Y, InDrawMode);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FTimingEventsTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	const FTimingViewLayout& Layout = Viewport.GetLayout();

	const float TopLaneY = GetPosY() + 1.0f + Layout.TimelineDY; // +1.0f is for horizontal line between timelines
	const float DY = InPosY - TopLaneY;

	// If mouse is not above first sub-track or below last sub-track...
	if (DY >= 0 && DY < GetHeight() - 1.0f - 2 * Layout.TimelineDY)
	{
		const int32 Depth = DY / (Layout.EventH + Layout.EventDY);

		auto EventFilter = [Depth](double, double, uint32 EventDepth)
		{
			return EventDepth == Depth;
		};

		const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();

		const double StartTime0 = Viewport.SlateUnitsToTime(InPosX);
		const double EndTime0 = StartTime0;
		TSharedPtr<const ITimingEvent> FoundEvent = SearchEvent(FTimingEventSearchParameters(StartTime0, EndTime0, ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));

		if (!FoundEvent.IsValid())
		{
			const double StartTime = StartTime0;
			const double EndTime = StartTime0 + SecondsPerPixel; // +1px
			FoundEvent = SearchEvent(FTimingEventSearchParameters(StartTime, EndTime, ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));
		}

		if (!FoundEvent.IsValid())
		{
			const double StartTime = StartTime0 - SecondsPerPixel; // -1px
			const double EndTime = StartTime0 + 2.0 * SecondsPerPixel; // +2px
			FoundEvent = SearchEvent(FTimingEventSearchParameters(StartTime, EndTime, ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));
		}

		return FoundEvent;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<ITimingEventFilter> FTimingEventsTrack::GetFilterByEvent(const TSharedPtr<const ITimingEvent> InTimingEvent) const
{
	if (InTimingEvent.IsValid() && InTimingEvent->Is<FTimingEvent>())
	{
		const FTimingEvent& Event = InTimingEvent->As<FTimingEvent>();
		TSharedRef<FTimingEventFilter> EventFilterRef = MakeShared<FTimingEventFilterByEventType>(Event.GetType());
		return EventFilterRef;
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
