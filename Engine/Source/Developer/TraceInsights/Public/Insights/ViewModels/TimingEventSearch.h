// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Search behavior flags
enum class ETimingEventSearchFlags : int32
{
	// Search all matches
	None = 0,
	SearchAll = None,

	// Whether to stop at the first match
	StopAtFirstMatch = (1 << 0),
};

ENUM_CLASS_FLAGS(ETimingEventSearchFlags);

// Parameters for a timing event search
class FTimingEventSearchParameters
{
public:
	// Predicate called to filter event matches. 
	// Note it is assumed that the SearchPredicate (below) will perform basic range checks on StartTime and EndTime,
	// therefore this is for any other filtering that needs to take place (e.g. left of an event, below an event etc.)
	// Returns true to pass the filter.
	typedef TFunctionRef<bool(double /*StartTime*/, double /*EndTime*/, uint32 /*Depth*/)> EventFilterPredicate;

	// Predicate called when we get a match
	typedef TFunctionRef<void(double /*StartTime*/, double /*EndTime*/, uint32 /*Depth*/)> EventMatchedPredicate;

	FTimingEventSearchParameters(double InStartTime, double InEndTime, ETimingEventSearchFlags Flags, EventFilterPredicate InEventFilter = NoFilter, EventMatchedPredicate InEventMatched = NoMatch)
		: EventFilter(InEventFilter)
		, EventMatched(InEventMatched)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, Flags(Flags)
	{}

private:
	// Default predicates
	static bool NoFilter(double, double, uint32) { return true; }
	static void NoMatch(double, double, uint32) {}

public:
	// Predicate for search filtering
	EventFilterPredicate EventFilter;

	// Predicate called when we get a match
	EventMatchedPredicate EventMatched;

	// Start time of the search
	double StartTime;

	// End time of the search
	double EndTime;

	// Search behavior flags
	ETimingEventSearchFlags Flags;
};

// Helper used to orchestrate a search of a timing event track's events
// Example of usage:
// 
//		FMyStruct MatchedPayload;
//
//		TTimingEventSearch<FMyStruct>::Search(
//		Parameters,
// 		[](const TTimingEventSearch<FMyStruct>::FContext& InContext)
// 		{
// 			for(FMyStruct& Payload : Payloads)
//			{
// 				InContext.Check(InEventStartTime, InEventEndTime, 0, Payload);
// 			}
// 		},
// 		[&MatchedPayload](double InStartTime, double InEndTime, uint32 InDepth, const FMyStruct& InEvent)
// 		{
// 			MatchedPayload = InEvent;
// 		},
// 		[&MatchedPayload](double InStartTime, double InEndTime, uint32 InDepth)
// 		{
// 			// Do something with MatchedPayload, e.g. call a captured lambda.
// 		});
// 
template <typename PayloadType>
struct TTimingEventSearch
{
public:
	struct FContext;

	// Predicate called when a match has been made to the search parameters
	typedef TFunctionRef<void(double /*InStartTime*/, double /*InEndTime*/, uint32 /*InDepth*/, const PayloadType& /*InPayload*/)> PayloadMatchedPredicate;

	// Predicate called to filter by payload contents
	// Return true to pass the filer
	typedef TFunctionRef<bool(double /*InStartTime*/, double /*InEndTime*/, uint32 /*InDepth*/, const PayloadType& /*InPayload*/)> PayloadFilterPredicate;

	// Predicate called when a match has been found. Note does not include payload as it is expected this will be captured/stored externally.
	typedef TFunctionRef<void(double /*InStartTime*/, double /*InEndTime*/, uint32 /*InDepth*/)> FoundPredicate;

	// Predicate called to run the search, e.g. iterate over an array of events
	// It is expected to call FContext::Check on each valid searched event.
	typedef TFunctionRef<void(FContext& /*Context*/)> SearchPredicate;

	// Context used to operate a search.
	struct FContext
	{
	public:
		FContext(const FTimingEventSearchParameters& InParameters, PayloadFilterPredicate InPayloadFilterPredicate, PayloadMatchedPredicate InPayloadMatchedPredicate)
			: Parameters(InParameters)
			, PayloadMatched(InPayloadMatchedPredicate)
			, PayloadFilter(InPayloadFilterPredicate)
			, FoundStartTime(-1.0)
			, FoundEndTime(-1.0)
			, FoundDepth(0)
			, bFound(false)
			, bContinueSearching(true)
		{
		}

		// Function called to check and potentially match an event
		void Check(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const PayloadType& InEvent)
		{
			if (bContinueSearching && Parameters.EventFilter(InEventStartTime, InEventEndTime, InEventDepth) && PayloadFilter(InEventStartTime, InEventEndTime, InEventDepth, InEvent))
			{
				Parameters.EventMatched(InEventStartTime, InEventEndTime, InEventDepth);
				PayloadMatched(InEventStartTime, InEventEndTime, InEventDepth, InEvent);

				FoundDepth = InEventDepth;
				FoundStartTime = InEventStartTime;
				FoundEndTime = InEventEndTime;

				bFound = true;
				bContinueSearching = (Parameters.Flags & ETimingEventSearchFlags::StopAtFirstMatch) == ETimingEventSearchFlags::None;
			}
		}

		// Access the search parameters
		const FTimingEventSearchParameters& GetParameters() const { return Parameters; }

		// Accessors for read-only results
		double GetStartTimeFound() const { return FoundStartTime; }
		double GetEndTimeFound() const { return FoundEndTime; }
		uint32 GetDepthFound() const { return FoundDepth; }
		bool IsMatchFound() const { return bFound; }
		bool ShouldContinueSearching() const { return bContinueSearching; }

	private:
		// Search parameters
		FTimingEventSearchParameters Parameters;

		// Predicate called when an event was matched
		PayloadMatchedPredicate PayloadMatched;

		// Filter applied to payloads
		PayloadFilterPredicate PayloadFilter;

		// The start time of the event that was found
		double FoundStartTime;

		// The end time of the event that was found
		double FoundEndTime;

		// The depth of the event that was found
		uint32 FoundDepth;

		// Whether a match was found
		bool bFound;

		// Internal flag to skip work if a match was found
		bool bContinueSearching;
	};

public:
	// Search using only the event filter
	static bool Search(const FTimingEventSearchParameters& InParameters, SearchPredicate InSearchPredicate, PayloadMatchedPredicate InPayloadMatchedPredicate, FoundPredicate InFoundPredicate)
	{
		auto NoFilter = [](double, double, uint32, const PayloadType&){ return true; };
		return Search(InParameters, InSearchPredicate, NoFilter, InPayloadMatchedPredicate, InFoundPredicate);
	}

	// Search using a specific payload filter
	static bool Search(const FTimingEventSearchParameters& InParameters, SearchPredicate InSearchPredicate, PayloadFilterPredicate InFilterPredicate, PayloadMatchedPredicate InPayloadMatchedPredicate, FoundPredicate InFoundPredicate)
	{
		FContext Context(InParameters, InFilterPredicate, InPayloadMatchedPredicate);

		InSearchPredicate(Context);

		if(Context.IsMatchFound())
		{
			InFoundPredicate(Context.GetStartTimeFound(), Context.GetEndTimeFound(), Context.GetDepthFound());
		}

		return Context.IsMatchFound();
	}
};