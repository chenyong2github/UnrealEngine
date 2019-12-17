// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/TimingEventsTrack.h"

#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"

#define LOCTEXT_NAMESPACE "TimingEventsTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FTimingEvent::TypeName = FName(TEXT("FTimingEvent"));

bool FTimingEventsTrack::bUseDownSampling = true;

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingEventsTrack::FTimingEventsTrack(const FName& InType, const FName& InSubType, const FString& InName)
	: FBaseTimingTrack(InType, InSubType, InName)
	, NumLanes(0)
	, DrawState(MakeShared<FTimingEventsTrackDrawState>())
{
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
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::PreUpdate(const ITimingTrackUpdateContext& Context)
{
	if (IsDirty() || Context.GetViewport().IsHorizontalViewportDirty())
	{
		ClearDirtyFlag();

		FTimingEventsTrackDrawStateBuilder Builder(*DrawState, Context.GetViewport());

		BuildDrawState(Builder, Context);

		Builder.Flush();

		SetNumLanes(Builder.GetMaxDepth() + 1);
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
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
	Helper.DrawEvents(*DrawState, *this);
	Helper.DrawTrackHeader(*this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingEventsTrack::DrawEvents(const ITimingTrackDrawContext& Context, const float OffsetY) const
{
	const FTimingViewDrawHelper& Helper = *static_cast<const FTimingViewDrawHelper*>(&Context.GetHelper());
	Helper.DrawEvents(*DrawState, *this, OffsetY);
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
	if (InTimingEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InTimingEvent))
	{
		const FTimingEvent& TrackEvent = static_cast<const FTimingEvent&>(InTimingEvent);
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

		const double StartTime = Viewport.SlateUnitsToTime(InPosX) - 1.0 / Viewport.GetScaleX(); // -1px
		const double EndTime = StartTime + 3.0 / Viewport.GetScaleX(); // +3px

		auto EventFilter = [Depth](double, double, uint32 EventDepth)
		{
			return EventDepth == Depth;
		};

		return SearchEvent(FTimingEventSearchParameters(StartTime, EndTime, ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
