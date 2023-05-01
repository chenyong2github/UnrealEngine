// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformTime.h"

namespace UE::Analytics
{
	// Generic telemetry data for a timed telemetry event
	struct FTimedTelemetryEvent
	{
		// Status code that can be used by an event to convey some custom data
		uint64 StatusCode = 0;
		// Duration of the event in seconds
		double Duration = 0.0;
	};
	
	/**
	 * A tracker of timespans with finite duration to send to telemetry
	 * It is intended for relatively long-duration timespans to be measured in seconds to minutes using
	 * a module which registers an IAnalyticsProviderET and starts an analytics session
	 * For profiling at a fine-grained level, use the Trace system
	 */
	template<typename EventT, typename EventDelegateT>
	class TTimedTelemetryEvent
	{
	public:
		TTimedTelemetryEvent() {}
		/**
		 * EventDelegate is the callback to be used when the timespan completes
		 */
		explicit TTimedTelemetryEvent(const EventDelegateT& EventDelegate);
		TTimedTelemetryEvent(TTimedTelemetryEvent&& Other);
		TTimedTelemetryEvent& operator=(TTimedTelemetryEvent&& Other);
	
		TTimedTelemetryEvent(TTimedTelemetryEvent&) = delete;
		TTimedTelemetryEvent& operator=(TTimedTelemetryEvent&) = delete;

		/**
		 * Marks an explicit beginning of the timespan to measure
		 */
		void MarkBegin();
		/**
		 * Marks an explicit ending of the timespan to measure
		 */
		void MarkEnd();

		/**
		 * Sets the StatusCode of the event
		 * The StatusCode can be used to convey limited information about the event itself
		 */
		void SetStatusCode(uint64 Code) { StatusCode = Code; }

		/**
		 * Get the StatusCode of the event
		 * The StatusCode can be used to convey limited information about the event itself
		 */
		uint64 GetStatusCode() const { return StatusCode; }

		/**
		 * Mutable access to the underlying event to set additional metadata
		 */
		EventT& GetEvent() { return Event; }
	
	private:
		//~ Track begin and end to prevent multiple events from being sent
		bool MarkedBegin = false;
		bool MarkedEnd = false;
		// Cache StatusCode until the event ends
		uint64 StatusCode = 0;
		// Cache BeginTime of event to compute duration
		double BeginTime = 0.0;
		EventT Event;
		const EventDelegateT* EventDelegate = nullptr;
	};


	template <typename EventT, typename EventDelegateT>
	TTimedTelemetryEvent<EventT, EventDelegateT>::TTimedTelemetryEvent(const EventDelegateT& InEventDelegate)
		: EventDelegate(&InEventDelegate)
	{
		static_assert(TIsDerivedFrom<EventT, FTimedTelemetryEvent>::Value, "EventT must be derived from FTimedTelemetryEvent");
	}

	template <typename EventT, typename EventDelegateT>
	TTimedTelemetryEvent<EventT, EventDelegateT>::TTimedTelemetryEvent(TTimedTelemetryEvent&& Other)
	{
		MarkedBegin = Other.MarkedBegin;
		MarkedEnd = Other.MarkedEnd;
		StatusCode = Other.StatusCode;
		BeginTime = Other.BeginTime;
		Event = MoveTemp(Other.Event);
		EventDelegate = Other.EventDelegate;

		Other.MarkedBegin = false;
		Other.MarkedEnd = false;
		Other.StatusCode = 0;
		Other.BeginTime = 0.0;
		Other.EventDelegate = nullptr;
	}
	
	template <typename EventT, typename EventDelegateT>
	TTimedTelemetryEvent<EventT, EventDelegateT>& TTimedTelemetryEvent<EventT, EventDelegateT>::operator=(TTimedTelemetryEvent&& Other)
	{
		if (this != &Other)
		{
			MarkedBegin = Other.MarkedBegin;
			MarkedEnd = Other.MarkedEnd;
			StatusCode = Other.StatusCode;
			BeginTime = Other.BeginTime;
			Event = MoveTemp(Other.Event);
			EventDelegate = Other.EventDelegate;

			Other.MarkedBegin = false;
			Other.MarkedEnd = false;
			Other.StatusCode = 0;
			Other.BeginTime = 0.0;
			Other.EventDelegate = nullptr;
		}
		return *this;
	}
	
	template <typename EventT, typename EventDelegateT>
	void TTimedTelemetryEvent<EventT, EventDelegateT>::MarkBegin()
	{
		if (EventDelegate->IsBound() && !MarkedEnd && !MarkedBegin)
		{
			MarkedBegin = true;
			BeginTime = FPlatformTime::Seconds();
		}
	}

	template <typename EventT, typename EventDelegateT>
	void TTimedTelemetryEvent<EventT, EventDelegateT>::MarkEnd()
	{
		if (MarkedBegin && !MarkedEnd)
		{
			// NOTE: May result in large durations on processes that suspend
			Event.Duration = FPlatformTime::Seconds() - BeginTime;
			// Send the cached ErrorCode along with the event
			Event.StatusCode = StatusCode;

			// No need to check IsBound - shouldn't be possible to be here if not bound
			EventDelegate->Execute(Event);
		}
	}

	// A RAII Wrapper for TimedTelemetryEvents
	template<typename EventT, typename EventDelegateT>
	class TScopedTimedTelemetryEvent
	{
	private:
		using TimedTelemetryEventT = TTimedTelemetryEvent<EventT, EventDelegateT>;
	public:		
		TScopedTimedTelemetryEvent(const EventDelegateT& EventDelegate);
		~TScopedTimedTelemetryEvent();

		void SetStatusCode(uint64 StatusCode) { TimedTelemetryEvent.SetStatusCode(StatusCode); }
		uint64 GetStatusCode() const { return TimedTelemetryEvent.GetStatusCode(); }

		EventT& GetEvent() { return TimedTelemetryEvent.GetEvent(); }

	private:
		TimedTelemetryEventT TimedTelemetryEvent;
	};

	template<typename EventT, typename EventDelegateT>
	inline TScopedTimedTelemetryEvent<EventT, EventDelegateT>::TScopedTimedTelemetryEvent(const EventDelegateT& EventDelegate)
		: TimedTelemetryEvent(EventDelegate)
	{
		TimedTelemetryEvent.MarkBegin();
	}

	template<typename EventT, typename EventDelegateT>
	inline TScopedTimedTelemetryEvent<EventT, EventDelegateT>::~TScopedTimedTelemetryEvent()
	{
		TimedTelemetryEvent.MarkEnd();
	}

	// Helper to create a Scoped Timed Telemetry Event
	template<typename EventT, typename EventDelegateT>
	TScopedTimedTelemetryEvent<EventT, EventDelegateT> MakeScopedTimedTelemetryEvent(const EventDelegateT& EventDelegate)
	{
		return TScopedTimedTelemetryEvent<EventT, EventDelegateT>(EventDelegate);
	}
}