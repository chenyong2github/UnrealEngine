// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	FTimingEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth)
		: Track(InTrack)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, ExclusiveTime(0.0)
		, Depth(InDepth)
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

	static const FName TypeName;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
