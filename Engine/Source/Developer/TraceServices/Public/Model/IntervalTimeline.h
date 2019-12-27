// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Containers/Timelines.h"
#include "Common/PagedArray.h"
#include "TraceServices/Containers/Allocators.h"
#include <limits>

namespace Trace
{

struct FIntervalTimelineDefaultSettings
{
	enum
	{
		EventsPerPage = 65536
	};
};

template<typename EventType, typename SettingsType = FIntervalTimelineDefaultSettings>
class TIntervalTimeline
	: public ITimeline<EventType>
{
public:
	TIntervalTimeline(ILinearAllocator& Allocator)
		: Events(Allocator, SettingsType::EventsPerPage)
	{
	}

	virtual ~TIntervalTimeline() = default;
	virtual uint64 GetModCount() const override { return ModCount; }
	virtual uint64 GetEventCount() const override { return Events.Num(); }
	virtual const EventType& GetEvent(uint64 InIndex) const override { return Events[InIndex].Event; }
	virtual double GetStartTime() const override { return Events.Num() > 0 ? Events[0].StartTime : 0.0; }
	virtual double GetEndTime() const override { return Events.Num() > 0 ? Events[Events.Num() - 1].EndTime : 0.0; }
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, typename ITimeline<EventType>::EventCallback Callback) const override { check(false); }
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, typename ITimeline<EventType>::EventRangeCallback Callback) const override { check(false); }
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, typename  ITimeline<EventType>::EventCallback Callback) const override
	{
		EnumerateEvents(IntervalStart, IntervalEnd, [&Callback](double InStartTime, double InEndTime, uint32 InDepth, const EventType& InEvent)
		{
			Callback(true, InStartTime, InEvent);
			Callback(false, InEndTime, InEvent);
		});
	}
	
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, typename ITimeline<EventType>::EventRangeCallback Callback) const override
	{
		auto EventIterator = Events.GetIteratorFromPage(0);
		const FEventInternal* Event = EventIterator.GetCurrentItem();
		while (Event)
		{
			const FEventPage* Page = EventIterator.GetCurrentPage();
			if (Page->EndTime < IntervalStart || IntervalEnd < Page->BeginTime)
			{
				if (!EventIterator.NextPage())
				{
					return;
				}
				Event = EventIterator.GetCurrentItem();
			}
			else
			{
				if (!(Event->EndTime < IntervalStart || IntervalEnd < Event->StartTime))
				{
					Callback(Event->StartTime, Event->EndTime, 0, Event->Event);
				}
				Event = EventIterator.NextItem();
			}
		}
	}
	
	uint64 AppendBeginEvent(double StartTime, const EventType& Event)
	{
		//check(StartTime >= LastTime);
		//LastTime = StartTime;

		uint64 Index = Events.Num();
		FEventInternal& EventInternal = Events.PushBack();
		EventInternal.StartTime = StartTime;
		EventInternal.EndTime = std::numeric_limits<double>::infinity();
		EventInternal.Event = Event;

		FEventPage* LastPage = Events.GetLastPage();
		if (LastPage != CurrentPage)
		{
			CurrentPage = LastPage;
			LastPage->BeginTime = StartTime;
			LastPage->EndTime = StartTime;
			LastPage->BeginEventIndex = Index;
		}
		else
		{
			LastPage->BeginTime = FMath::Min(LastPage->BeginTime, StartTime);
			LastPage->EndTime = FMath::Max(LastPage->EndTime, StartTime);
		}

		LastPage->EndEventIndex = Index + 1;

		++ModCount;

		return Index;
	}

	EventType& EndEvent(uint64 EventIndex, double EndTime)
	{
		//check(EndTime >= LastTime);
		//LastTime = EndTime;
		FEventInternal& EventInternal = Events[EventIndex];
		check(EventIndex < Events.Num());
		EventInternal.EndTime = EndTime;
		FEventPage* Page = Events.GetItemPage(EventIndex);
		Page->EndTime = FMath::Max(Page->EndTime, EndTime);

		++ModCount;

		return EventInternal.Event;
	}

private:
	struct FEventInternal
	{
		double StartTime;
		double EndTime;
		EventType Event;
	};

	struct FEventPage
	{
		double BeginTime = 0.0;
		double EndTime = 0.0;
		uint64 BeginEventIndex = 0;
		uint64 EndEventIndex = 0;
		FEventInternal* Items = nullptr;
		uint64 Count = 0;
	};

	TPagedArray<FEventInternal, FEventPage> Events;
	FEventPage* CurrentPage = nullptr;
	double LastTime = 0.0;
	uint64 ModCount = 0;
};

}