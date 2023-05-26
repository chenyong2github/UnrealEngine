// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeDebuggerTrack.h"
#include "Debugger/StateTreeDebugger.h"
#include "SStateTreeDebuggerEventTimelineView.h"
#include "Styling/AppStyle.h"

//----------------------------------------------------------------------//
// FStateTreeTrack
//----------------------------------------------------------------------//
FStateTreeDebuggerTrack::FStateTreeDebuggerTrack(const TSharedPtr<FStateTreeDebugger>& InDebugger, const FStateTreeInstanceDebugId InInstanceId, const FText InName, const TRange<double>& InViewRange)
	: TrackName(InName)
	, StateTreeDebugger(InDebugger)
	, InstanceId(InInstanceId)
	, ViewRange(InViewRange)
{
	EventData = MakeShared<SStateTreeDebuggerEventTimelineView::FTimelineEventData>();
}

TSharedPtr<SWidget> FStateTreeDebuggerTrack::GetDetailsViewInternal() 
{
	return SNullWidget::NullWidget;
}

bool FStateTreeDebuggerTrack::UpdateInternal()
{
	const int32 PrevNumPoints = EventData->Points.Num();
	const int32 PrevNumWindows = EventData->Windows.Num();

	EventData->Points.SetNum(0, false);
	EventData->Windows.SetNum(0);
	
	const FStateTreeDebugger* Debugger = StateTreeDebugger.Get();
	check(Debugger);
	const UStateTree* StateTree = Debugger->GetAsset();
	const UE::StateTreeDebugger::FInstanceEventCollection& EventCollection = Debugger->GetEventCollection(InstanceId);
	const double RecordingDuration = Debugger->GetRecordingDuration();
	
	if (StateTree != nullptr && EventCollection.IsValid())
	{
		auto MakeRandomColor = [bActive = !bIsStale](const uint32 InSeed)->FLinearColor
		{
			const FRandomStream Stream(InSeed);
			const uint8 Hue = static_cast<uint8>(Stream.FRand() * 255.0f);
			const uint8 SatVal = bActive ? 196 : 128;
			return FLinearColor::MakeFromHSV8(Hue, SatVal, SatVal);
		};
		
		const TConstArrayView<UE::StateTreeDebugger::FFrameSpan> Spans = EventCollection.FrameSpans;
		const TConstArrayView<FStateTreeTraceEventVariantType> Events = EventCollection.Events;
		const uint32 NumStateChanges = EventCollection.ActiveStatesChanges.Num();
		
		for (uint32 StateChangeIndex = 0; StateChangeIndex < NumStateChanges; ++StateChangeIndex)
		{
			const uint32 SpanIndex = EventCollection.ActiveStatesChanges[StateChangeIndex].SpanIndex;
			const uint32 EventIndex = EventCollection.ActiveStatesChanges[StateChangeIndex].EventIndex;
			const FStateTreeTraceActiveStatesEvent& Event = Events[EventIndex].Get<FStateTreeTraceActiveStatesEvent>();
				
			FString StatePath;
			for (int32 StateIndex = 0; StateIndex < Event.ActiveStates.Num(); StateIndex++)
			{
				const FCompactStateTreeState& State = StateTree->GetStates()[Event.ActiveStates[StateIndex].Index];
				StatePath.Appendf(TEXT("%s%s"), StateIndex == 0 ? TEXT("") : TEXT("."), *State.Name.ToString());
			}

			UE::StateTreeDebugger::FFrameSpan Span = EventCollection.FrameSpans[SpanIndex];
			SStateTreeDebuggerEventTimelineView::FTimelineEventData::EventWindow& Window = EventData->Windows.AddDefaulted_GetRef();
			Window.Color = MakeRandomColor(GetTypeHash(StatePath));
			Window.Description = FText::FromString(StatePath);
			Window.TimeStart = Span.GetWorldTimeStart();

			// When there is another state change after the current one in the list we use its start time to close the window.
			if (StateChangeIndex < NumStateChanges-1)
			{
				const uint32 NextSpanIndex = EventCollection.ActiveStatesChanges[StateChangeIndex+1].SpanIndex;
				Window.TimeEnd = EventCollection.FrameSpans[NextSpanIndex].GetWorldTimeStart();
			}
			else
			{
				Window.TimeEnd = Debugger->IsActiveInstance(RecordingDuration, InstanceId) ? RecordingDuration : Span.GetWorldTimeEnd();
			}
		}

		for (int32 SpanIndex = 0; SpanIndex < Spans.Num(); SpanIndex++)
		{
			const UE::StateTreeDebugger::FFrameSpan& Span = Spans[SpanIndex];

			const int32 StartIndex = Span.EventIdx;
			const int32 MaxIndex = (SpanIndex + 1 < Spans.Num()) ? Spans[SpanIndex+1].EventIdx : Events.Num();
			for (int EventIndex = StartIndex; EventIndex < MaxIndex; ++EventIndex)
			{
				if (Events[EventIndex].IsType<FStateTreeTraceLogEvent>())
				{
					SStateTreeDebuggerEventTimelineView::FTimelineEventData::EventPoint Point;
					Point.Time = Span.GetWorldTimeStart();
					Point.Color = FColorList::Salmon;
					EventData->Points.Add(Point);
				}
			}
		}
	}

	const bool bChanged = (PrevNumPoints != EventData->Points.Num() || PrevNumWindows != EventData->Windows.Num());
	return bChanged;
}

TSharedPtr<SWidget> FStateTreeDebuggerTrack::GetTimelineViewInternal()
{
	return SNew(SStateTreeDebuggerEventTimelineView)
		.ViewRange_Lambda([this](){ return ViewRange; })
		.EventData_Lambda([this](){ return EventData; });
}

#endif // WITH_STATETREE_DEBUGGER