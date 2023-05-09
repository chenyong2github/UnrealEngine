// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeDebuggerTypes.h"

namespace UE::StateTreeDebugger
{

//----------------------------------------------------------------//
// FInstanceDescriptor
//----------------------------------------------------------------//
FInstanceDescriptor::FInstanceDescriptor(const UStateTree* InStateTree, const FStateTreeInstanceDebugId InId, const FString& InName, const TRange<double> InLifetime)
	: Lifetime(InLifetime)
	, StateTree(InStateTree)
	, Name(InName)
	, Id(InId)
{ 
}

bool FInstanceDescriptor::IsValid() const
{
	return StateTree.IsValid() && Name.Len() && Id.IsValid();
}


//----------------------------------------------------------------//
// FInstanceEventCollection
//----------------------------------------------------------------//
const FInstanceEventCollection FInstanceEventCollection::Invalid;


//----------------------------------------------------------------//
// FScrubState
//----------------------------------------------------------------//
void FScrubState::SetScrubTime(const double NewScrubTime)
{
	if (EventCollectionIndex != INDEX_NONE)
	{
		const TArray<FFrameSpan>& Spans = EventCollections[EventCollectionIndex].FrameSpans;
		const uint32 NewFrameSpanIndex = Spans.IndexOfByPredicate([Time = NewScrubTime](const FFrameSpan& Span)
		{
			return Span.Frame.StartTime <= Time && Time <= Span.Frame.EndTime; 
		});

		if (NewFrameSpanIndex != INDEX_NONE)
		{
			SetFrameSpanIndex(NewFrameSpanIndex);
		}
		else if (Spans.Num() > 0 && NewScrubTime > Spans.Last().Frame.EndTime)
		{
			// Clamp to last span so it keep the scrub valid when sticking to most recent data.
			SetFrameSpanIndex(EventCollections[EventCollectionIndex].FrameSpans.Num() - 1);
		}
		else
		{
			TraceFrameIndex = INDEX_NONE;
			FrameSpanIndex = INDEX_NONE;
			ActiveStatesIndex = INDEX_NONE;
		}
	}

	// This will set back to the exact value provided since SetFrameSpanIndex will snap it to the start time of the matching frame.
	// It will be consistent with the case where EventCollectionIndex is not set.
	ScrubTime = NewScrubTime;
}

void FScrubState::SetFrameSpanIndex(const uint32 NewFrameSpanIndex)
{
	FrameSpanIndex = NewFrameSpanIndex;
	checkf(EventCollections.IsValidIndex(EventCollectionIndex), TEXT("Internal method expecting validity checks before getting called."));
	const FInstanceEventCollection& EventCollection = EventCollections[EventCollectionIndex];

	checkf(EventCollections[EventCollectionIndex].FrameSpans.IsValidIndex(FrameSpanIndex), TEXT("Internal method expecting validity checks before getting called."));
	ScrubTime = EventCollection.FrameSpans[FrameSpanIndex].Frame.StartTime;
	TraceFrameIndex = EventCollection.FrameSpans[FrameSpanIndex].Frame.Index;
	UpdateActiveStatesIndex();
}

void FScrubState::SetActiveStatesIndex(const uint32 NewActiveStatesIndex)
{
	ActiveStatesIndex = NewActiveStatesIndex;

	checkf(EventCollections.IsValidIndex(EventCollectionIndex), TEXT("Internal method expecting validity checks before getting called."));
	const FInstanceEventCollection& EventCollection = EventCollections[EventCollectionIndex];

	checkf(EventCollection.ActiveStatesChanges.IsValidIndex(ActiveStatesIndex), TEXT("Internal method expecting validity checks before getting called."));
	FrameSpanIndex = EventCollection.ActiveStatesChanges[ActiveStatesIndex].Key;
	ScrubTime = EventCollection.FrameSpans[FrameSpanIndex].Frame.StartTime;
	TraceFrameIndex = EventCollection.FrameSpans[FrameSpanIndex].Frame.Index;
}

bool FScrubState::HasPreviousFrame() const
{
	return IsInBounds() ? EventCollections[EventCollectionIndex].FrameSpans.IsValidIndex(FrameSpanIndex-1) : false;
}

double FScrubState::GotoPreviousFrame()
{
	SetFrameSpanIndex(FrameSpanIndex - 1);
	return ScrubTime;
}

bool FScrubState::HasNextFrame() const
{
	return IsInBounds() ? EventCollections[EventCollectionIndex].FrameSpans.IsValidIndex(FrameSpanIndex+1) : false;
}

double FScrubState::GotoNextFrame()
{
	SetFrameSpanIndex(FrameSpanIndex + 1);
	return ScrubTime;
}

bool FScrubState::HasPreviousActiveStates() const
{
	return (EventCollectionIndex != INDEX_NONE && ActiveStatesIndex != INDEX_NONE)
		? EventCollections[EventCollectionIndex].ActiveStatesChanges.IsValidIndex(ActiveStatesIndex-1)
		: false;
}

double FScrubState::GotoPreviousActiveStates()
{
	SetActiveStatesIndex(ActiveStatesIndex - 1);
	return ScrubTime;
}

bool FScrubState::HasNextActiveStates() const
{
	return (EventCollectionIndex != INDEX_NONE && ActiveStatesIndex != INDEX_NONE)
		? EventCollections[EventCollectionIndex].ActiveStatesChanges.IsValidIndex(ActiveStatesIndex+1)
		: false;
}

double FScrubState::GotoNextActiveStates()
{
	SetActiveStatesIndex(ActiveStatesIndex + 1);
	return ScrubTime;
}

const FInstanceEventCollection& FScrubState::GetEventCollection() const
{
	return EventCollectionIndex != INDEX_NONE ? EventCollections[EventCollectionIndex] : FInstanceEventCollection::Invalid;
}

void FScrubState::UpdateActiveStatesIndex()
{
	check(EventCollectionIndex != INDEX_NONE);
	const FInstanceEventCollection& EventCollection = EventCollections[EventCollectionIndex];

	// Need to find the index of a frame span that contains an active states changed event; either the current one has it otherwise look backward to find the last one
	ActiveStatesIndex = EventCollection.ActiveStatesChanges.FindLastByPredicate([CurrentFrameSpanIndex = FrameSpanIndex](const TTuple<uint32, uint32> SpanAndEventIndices)
	{
		return SpanAndEventIndices.Key <= CurrentFrameSpanIndex;
	});
}

} // UE::StateTreeDebugger
#endif // WITH_STATETREE_DEBUGGER
