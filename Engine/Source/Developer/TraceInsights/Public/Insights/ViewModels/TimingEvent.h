// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/ITimingEvent.h"
#include "Insights/ViewModels/TimingEventSearch.h"

class FBaseTimingTrack;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FTimingEvent : public ITimingEvent
{
public:
	FTimingEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth, uint64 InType = uint64(-1))
		: Track(InTrack)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, ExclusiveTime(0.0)
		, Depth(InDepth)
		, Type(InType)
	{}

	virtual ~FTimingEvent() {}

	FTimingEvent(const FTimingEvent&) = default;
	FTimingEvent& operator=(const FTimingEvent&) = default;

	FTimingEvent(FTimingEvent&&) = default;
	FTimingEvent& operator=(FTimingEvent&&) = default;

	//////////////////////////////////////////////////
	// ITimingEvent interface

	virtual const FName& GetTypeName() const override { return FTimingEvent::TypeName; }

	virtual const TSharedRef<const FBaseTimingTrack> GetTrack() const override { return Track; }

	virtual uint32 GetDepth() const override { return Depth; }

	virtual double GetStartTime() const override { return StartTime; }
	virtual double GetEndTime() const override { return EndTime; }
	virtual double GetDuration() const override { return EndTime - StartTime; }

	virtual bool Equals(const ITimingEvent& Other) const override
	{
		if (GetTypeName() != Other.GetTypeName())
		{
			return false;
		}

		const FTimingEvent& OtherTimingEvent = static_cast<const FTimingEvent&>(Other);
		return Track == Other.GetTrack()
			&& Depth == OtherTimingEvent.GetDepth()
			&& StartTime == OtherTimingEvent.GetStartTime()
			&& EndTime == OtherTimingEvent.GetEndTime();
	}

	//////////////////////////////////////////////////

	FTimingEventSearchHandle& GetSearchHandle() const { return SearchHandle; }

	double GetExclusiveTime() const { return ExclusiveTime; }
	void SetExclusiveTime(double InExclusiveTime) { ExclusiveTime = InExclusiveTime; }

	uint64 GetType() const { return Type; }

	static const FName& GetStaticTypeName() { return FTimingEvent::TypeName; }
	static bool CheckTypeName(const ITimingEvent& Event) { return Event.GetTypeName() == FTimingEvent::TypeName; }

private:
	// The track this timing event is contained within
	TSharedRef<const FBaseTimingTrack> Track;

	// Handle to a previous search, used to accelerate access to underlying event data
	mutable FTimingEventSearchHandle SearchHandle;

	// The start time of the event
	double StartTime;

	// The end time of the event
	double EndTime;

	// For hierarchical events, the cached exclusive time
	double ExclusiveTime;

	// The depth of the event
	uint32 Depth;

	// The event type (category, group id, etc.).
	uint64 Type;

	static const FName TypeName;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FTimingEventFilter : public ITimingEventFilter
{
public:
	FTimingEventFilter()
		: bFilterByTrackType(false)
		, TrackType(NAME_None)
		, bFilterByEventType(false)
		, EventType(uint64(-1))
		, bFilterByMinDuration(false)
		, MinDuration(0.0)
	{}

	virtual ~FTimingEventFilter() {}

	FTimingEventFilter(const FTimingEventFilter&) = default;
	FTimingEventFilter& operator=(const FTimingEventFilter&) = default;

	FTimingEventFilter(FTimingEventFilter&&) = default;
	FTimingEventFilter& operator=(FTimingEventFilter&&) = default;

	//////////////////////////////////////////////////
	// ITimingEventFilter interface

	virtual const FName& GetTypeName() const override { return FTimingEventFilter::TypeName; }

	virtual bool FilterTrack(const FBaseTimingTrack& InTrack) const override;

	virtual bool FilterEvent(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType = 0, uint32 InEventColor = 0) const override
	{
		return (!bFilterByEventType || InEventType == EventType)
			&& (!bFilterByMinDuration || InEventEndTime - InEventStartTime >= MinDuration)
			&& (!bFilterByMaxDuration || InEventEndTime - InEventStartTime <= MaxDuration);
	}

	virtual uint32 GetChangeNumber() const override { return ChangeNumber; }

	//////////////////////////////////////////////////

	bool IsFilteringByTrackType() const { return bFilterByTrackType; }
	const FName& GetTrackType() const { return TrackType; }
	void SetFilterByTrackType(bool bInFilterByTrackType) { if (bFilterByTrackType != bInFilterByTrackType) { bFilterByTrackType = bInFilterByTrackType; ChangeNumber++; } }
	void SetTrackType(const FName InTrackType) { if (TrackType != InTrackType) { TrackType = InTrackType; ChangeNumber++; } }

	bool IsFilteringByEventType() const { return bFilterByEventType; }
	uint64 GetEventType() const { return EventType; }
	void SetFilterByEventType(bool bInFilterByEventType) { if (bFilterByEventType != bInFilterByEventType) { bFilterByEventType = bInFilterByEventType; ChangeNumber++; } }
	void SetEventType(uint64 InEventType) { if (EventType != InEventType) { EventType = InEventType; ChangeNumber++; } }

	bool IsFilteringByMinDuration() const { return bFilterByMinDuration; }
	double GetMinDuration() const { return MinDuration; }

	bool IsFilteringByMaxDuration() const { return bFilterByMaxDuration; }
	double GetMaxDuration() const { return MaxDuration; }

	static const FName& GetStaticTypeName() { return FTimingEventFilter::TypeName; }
	static bool CheckTypeName(const ITimingEventFilter& EventFilter) { return EventFilter.GetTypeName() == FTimingEventFilter::TypeName; }

private:
	bool FilterEventByType(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType, uint32 InEventColor) const
	{
		return InEventType == EventType;
	}
	bool FilterEventByMinDuration(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType, uint32 InEventColor) const
	{
		return InEventEndTime - InEventStartTime >= MinDuration;
	}
	bool FilterEventByMaxDuration(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType, uint32 InEventColor) const
	{
		return InEventEndTime - InEventStartTime <= MaxDuration;
	}

private:
	bool bFilterByTrackType;
	FName TrackType;

	bool bFilterByEventType;
	uint64 EventType;

	bool bFilterByMinDuration;
	double MinDuration;

	bool bFilterByMaxDuration;
	double MaxDuration;

	uint32 ChangeNumber;

	static const FName TypeName;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
