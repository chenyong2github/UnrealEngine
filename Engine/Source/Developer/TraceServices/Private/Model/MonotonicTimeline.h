// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Common/PagedArray.h"

namespace Trace
{

struct FMonotonicTimelineDefaultSettings
{
	enum
	{
		MaxDepth = 1024,
		ScopeEntriesPageSize = 65536,
		EventsPageSize = 65536,
		DetailLevelsCount = 6,
	};

	constexpr static double DetailLevelResolution(int32 Index)
	{
		const double DetailLevels[DetailLevelsCount] = { 0.0, 0.0001, 0.001, 0.008, 0.04, 0.2 };
		return DetailLevels[Index];
	}
};

template<typename InEventType, typename SettingsType = FMonotonicTimelineDefaultSettings>
class TMonotonicTimeline
	: public ITimeline<InEventType>
{
public:
	using EventType = InEventType;

	TMonotonicTimeline(ILinearAllocator& InAllocator)
		: Allocator(InAllocator)
	{
		
		for (int32 DetailLevelIndex = 0; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			double Resolution = SettingsType::DetailLevelResolution(DetailLevelIndex);
			DetailLevels.Emplace(Allocator, Resolution);
		}
	}

	virtual ~TMonotonicTimeline() = default;
	
	void AppendBeginEvent(double StartTime, const EventType& Event)
	{
		int32 CurrentDepth = DetailLevels[0].InsertionState.CurrentDepth;

		AddScopeEntry(DetailLevels[0], StartTime, true);
		AddEvent(DetailLevels[0], Event);
		FDetailLevelDepthState& Lod0DepthState = DetailLevels[0].InsertionState.DepthStates[CurrentDepth];
		Lod0DepthState.EnterTime = StartTime;
		Lod0DepthState.DominatingEvent = Event;
		//Lod0DepthState.DebugDominatingEventType = Owner.EventTypes[TypeId];

		for (int32 DetailLevelIndex = 1; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];
			FDetailLevelDepthState& CurrentDepthState = DetailLevel.InsertionState.DepthStates[CurrentDepth];

			if (CurrentDepthState.PendingScopeEnterIndex < 0 || StartTime >= CurrentDepthState.EnterTime + DetailLevel.Resolution)
			{
				for (int32 Depth = DetailLevel.InsertionState.PendingDepth; Depth >= CurrentDepth; --Depth)
				{
					FDetailLevelDepthState& DepthState = DetailLevel.InsertionState.DepthStates[Depth];
					check(DepthState.PendingScopeEnterIndex >= 0);
					AddScopeEntry(DetailLevel, DepthState.ExitTime, false);

					DepthState.PendingScopeEnterIndex = -1;
					DepthState.PendingEventIndex = -1;
				}
				DetailLevel.InsertionState.PendingDepth = CurrentDepth;

				uint64 EnterScopeIndex = DetailLevel.ScopeEntries.Num();
				uint64 EventIndex = DetailLevel.Events.Num();

				AddScopeEntry(DetailLevel, StartTime, true);
				AddEvent(DetailLevel, Event);

				CurrentDepthState.DominatingEventStartTime = StartTime;
				CurrentDepthState.DominatingEventEndTime = StartTime;
				CurrentDepthState.DominatingEventDuration = 0.0;
				CurrentDepthState.PendingScopeEnterIndex = EnterScopeIndex;
				CurrentDepthState.PendingEventIndex = EventIndex;
				CurrentDepthState.EnterTime = StartTime;
				CurrentDepthState.DominatingEvent = Event;
				//CurrentDepthState.DebugDominatingEventType = Owner.EventTypes[TypeId];
			}
			else if (CurrentDepth > DetailLevel.InsertionState.PendingDepth)
			{
				DetailLevel.InsertionState.PendingDepth = CurrentDepth;
			}
			SetEvent(DetailLevel, CurrentDepthState.PendingEventIndex, Event);
		}
		++ModCount;
	}

	void AppendEndEvent(double EndTime)
	{
		AddScopeEntry(DetailLevels[0], EndTime, false);

		int32 CurrentDepth = DetailLevels[0].InsertionState.CurrentDepth;
		for (int32 DetailLevelIndex = 1; DetailLevelIndex < SettingsType::DetailLevelsCount; ++DetailLevelIndex)
		{
			FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];

			DetailLevel.InsertionState.DepthStates[CurrentDepth].ExitTime = EndTime;

			UpdateDominatingEvent(DetailLevel, CurrentDepth, EndTime);
		}
		++ModCount;
	}

	virtual uint64 GetModCount() const override
	{
		return ModCount;
	}

	virtual uint64 GetEventCount() const override
	{
		return DetailLevels[0].Events.Num();
	}

	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(bool, double, const EventType&)> Callback) const override
	{
		int32 DetailLevelIndex = SettingsType::DetailLevelsCount - 1;
		for (; DetailLevelIndex > 0; --DetailLevelIndex)
		{
			if (DetailLevels[DetailLevelIndex].Resolution <= Resolution)
			{
				break;
			}
		}

		const FDetailLevel& DetailLevel = DetailLevels[DetailLevelIndex];
		if (DetailLevel.ScopeEntries.Num() == 0)
		{
			return;
		}

		uint64 FirstScopePageIndex = Algo::UpperBoundBy(DetailLevel.ScopeEntries, IntervalStart, [](const FEventScopeEntryPage& Page)
		{
			return Page.BeginTime;
		});
		if (FirstScopePageIndex > 0)
		{
			--FirstScopePageIndex;
		}
		auto ScopeEntryIterator = DetailLevel.ScopeEntries.GetIteratorFromPage(FirstScopePageIndex);
		const FEventScopeEntryPage* ScopePage = ScopeEntryIterator.GetCurrentPage();
		if (ScopePage->BeginTime > IntervalEnd)
		{
			return;
		}
		if (ScopePage->EndTime < IntervalStart)
		{
			return;
		}
		auto EventsIterator = DetailLevel.Events.GetIteratorFromItem(ScopePage->BeginEventIndex);
		struct FEnumerationStackEntry
		{
			double StartTime;
			EventType Event;
		};
		FEnumerationStackEntry EventStack[SettingsType::MaxDepth];
		int32 CurrentStackDepth = ScopePage->InitialStackCount;
		for (int32 InitialStackIndex = 0; InitialStackIndex < CurrentStackDepth; ++InitialStackIndex)
		{
			FEnumerationStackEntry& EnumerationStackEntry = EventStack[InitialStackIndex];
			const FEventStackEntry& EventStackEntry = ScopePage->InitialStack[InitialStackIndex];
			EnumerationStackEntry.StartTime = GetScopeEntryTime(DetailLevel, EventStackEntry.EnterScopeIndex);
			EnumerationStackEntry.Event = GetEvent(DetailLevel, EventStackEntry.EventIndex);
		}

		const FEventScopeEntry* ScopeEntry = ScopeEntryIterator.GetCurrentItem();
		const EventType* Event = EventsIterator.GetCurrentItem();
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) < IntervalStart)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEnumerationStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				StackEntry.StartTime = -ScopeEntry->Time;
				Event = EventsIterator.NextItem();
			}
			else
			{
				check(CurrentStackDepth > 0);
				--CurrentStackDepth;
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}
		if (CurrentStackDepth == 1 && EventStack[0].StartTime > IntervalEnd)
		{
			return;
		}
		for (int32 StackIndex = 0; StackIndex < CurrentStackDepth; ++StackIndex)
		{
			FEnumerationStackEntry& StackEntry = EventStack[StackIndex];
			Callback(true, StackEntry.StartTime, StackEntry.Event);
		}
		while (ScopeEntry && FMath::Abs(ScopeEntry->Time) <= IntervalEnd)
		{
			if (ScopeEntry->Time < 0.0)
			{
				check(CurrentStackDepth < SettingsType::MaxDepth);
				FEnumerationStackEntry& StackEntry = EventStack[CurrentStackDepth++];
				StackEntry.Event = *Event;
				Callback(true, -ScopeEntry->Time, StackEntry.Event);
				Event = EventsIterator.NextItem();
			}
			else
			{
				check(CurrentStackDepth > 0);
				FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
				Callback(false, ScopeEntry->Time, StackEntry.Event);
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}
		uint32 ExitDepth = 0;
		while (CurrentStackDepth > 0 && ScopeEntry)
		{
			if (ScopeEntry->Time < 0.0)
			{
				++ExitDepth;
			}
			else
			{
				if (ExitDepth == 0)
				{
					FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
					Callback(false, ScopeEntry->Time, StackEntry.Event);
				}
				else
				{
					--ExitDepth;
				}
			}
			ScopeEntry = ScopeEntryIterator.NextItem();
		}
		while (CurrentStackDepth > 0)
		{
			FEnumerationStackEntry& StackEntry = EventStack[--CurrentStackDepth];
			Callback(false, DetailLevel.InsertionState.LastTime, StackEntry.Event);
		}
	}

	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const override
	{
		struct FStackEntry
		{
			uint64 LocalEventIndex;
		};
		FStackEntry EventStack[SettingsType::MaxDepth];
		uint32 CurrentDepth = 0;

		struct FOutputEvent
		{
			double StartTime;
			double EndTime;
			uint32 Depth;
			EventType Event;
		};
		TArray<FOutputEvent> OutputEvents;

		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, Resolution, [&EventStack, &OutputEvents, &CurrentDepth, Callback](bool IsEnter, double Time, const EventType& Event)
		{
			if (IsEnter)
			{
				FStackEntry& StackEntry = EventStack[CurrentDepth];
				StackEntry.LocalEventIndex = OutputEvents.Num();
				FOutputEvent& OutputEvent = OutputEvents.AddDefaulted_GetRef();
				OutputEvent.StartTime = Time;
				OutputEvent.EndTime = Time;
				OutputEvent.Depth = CurrentDepth;
				OutputEvent.Event = Event;
				++CurrentDepth;
			}
			else
			{
				{
					FStackEntry& StackEntry = EventStack[--CurrentDepth];
					FOutputEvent* OutputEvent = OutputEvents.GetData() + StackEntry.LocalEventIndex;
					OutputEvent->EndTime = Time;
				}
				if (CurrentDepth == 0)
				{
					for (FOutputEvent& OutputEvent : OutputEvents)
					{
						Callback(OutputEvent.StartTime, OutputEvent.EndTime, OutputEvent.Depth, OutputEvent.Event);
					}
					OutputEvents.Empty(OutputEvents.Num());
				}
			}
		});
	}

	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(bool, double, const EventType&)> Callback) const override
	{
		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, 0.0, Callback);
	}

	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double, uint32, const EventType&)> Callback) const override
	{
		EnumerateEventsDownSampled(IntervalStart, IntervalEnd, 0.0, Callback);
	}

private:
	struct FEventScopeEntry
	{
		double Time;
		//uint32 DebugDepth;
	};

	struct FEventStackEntry
	{
		uint64 EnterScopeIndex;
		uint64 EventIndex;
	};

	struct FEventScopeEntryPage
	{
		FEventScopeEntry* Items = nullptr;
		uint64 Count = 0;
		double BeginTime = 0.0;
		double EndTime = 0.0;
		uint64 BeginEventIndex = 0;
		uint64 EndEventIndex = 0;
		FEventStackEntry* InitialStack = nullptr;
		uint16 InitialStackCount = 0;
	};

	struct FDetailLevelDepthState
	{
		int64 PendingScopeEnterIndex = -1;
		int64 PendingEventIndex = -1;

		EventType DominatingEvent;
		double DominatingEventStartTime = 0.0;
		double DominatingEventEndTime = 0.0;
		double DominatingEventDuration = 0.0;

		double EnterTime = 0.0;
		double ExitTime = 0.0;

		//const FTimelineEventType* DebugDominatingEventType;
	};

	struct FDetailLevelInsertionState
	{
		double LastTime = -1.0;
		uint16 CurrentDepth = 0;
		int32 PendingDepth = -1;
		FDetailLevelDepthState DepthStates[SettingsType::MaxDepth];
		FEventStackEntry EventStack[SettingsType::MaxDepth];
		FEventScopeEntryPage* CurrentScopeEntryPage = nullptr;
	};

	struct FDetailLevel
	{
		FDetailLevel(ILinearAllocator& Allocator, double InResolution)
			: Resolution(InResolution)
			, ScopeEntries(Allocator, SettingsType::ScopeEntriesPageSize)
			, Events(Allocator, SettingsType::EventsPageSize)
		{

		}

		double Resolution;
		TPagedArray<FEventScopeEntry, FEventScopeEntryPage> ScopeEntries;
		TPagedArray<EventType> Events;

		FDetailLevelInsertionState InsertionState;
	};

	void UpdateDominatingEvent(FDetailLevel& DetailLevel, int32 Depth, double CurrentTime)
	{
		FDetailLevelDepthState& Lod0DepthState = DetailLevels[0].InsertionState.DepthStates[Depth];
		double Lod0EventDuration = CurrentTime - Lod0DepthState.EnterTime;
		FDetailLevelDepthState& CurrentDepthState = DetailLevel.InsertionState.DepthStates[Depth];
		if (Lod0EventDuration > CurrentDepthState.DominatingEventDuration)
		{
			check(CurrentDepthState.PendingScopeEnterIndex >= 0);
			check(CurrentDepthState.PendingEventIndex >= 0);

			CurrentDepthState.DominatingEvent = Lod0DepthState.DominatingEvent;
			CurrentDepthState.DominatingEventStartTime = Lod0DepthState.EnterTime;
			CurrentDepthState.DominatingEventEndTime = CurrentTime;
			CurrentDepthState.DominatingEventDuration = Lod0EventDuration;

			SetEvent(DetailLevel, CurrentDepthState.PendingEventIndex, CurrentDepthState.DominatingEvent);

			//CurrentDepthState.DebugDominatingEventType = Owner.EventTypes[CurrentDepthState.DominatingEventType];
		}
	}

	void AddScopeEntry(FDetailLevel& DetailLevel, double Time, bool IsEnter)
	{
		check(Time >= DetailLevel.InsertionState.LastTime);
		DetailLevel.InsertionState.LastTime = Time;

		uint64 EventIndex = DetailLevel.Events.Num();
		uint64 ScopeIndex = DetailLevel.ScopeEntries.Num();

		FEventScopeEntry& ScopeEntry = DetailLevel.ScopeEntries.PushBack();
		ScopeEntry.Time = IsEnter ? -Time : Time;
		FEventScopeEntryPage* LastPage = DetailLevel.ScopeEntries.GetLastPage();
		if (LastPage != DetailLevel.InsertionState.CurrentScopeEntryPage)
		{
			DetailLevel.InsertionState.CurrentScopeEntryPage = LastPage;
			LastPage->BeginTime = Time;
			LastPage->BeginEventIndex = DetailLevel.Events.Num();
			LastPage->EndEventIndex = LastPage->BeginEventIndex;
			LastPage->InitialStackCount = DetailLevel.InsertionState.CurrentDepth;
			if (LastPage->InitialStackCount)
			{
				LastPage->InitialStack = reinterpret_cast<FEventStackEntry*>(Allocator.Allocate(LastPage->InitialStackCount * sizeof(FEventStackEntry)));
				memcpy(LastPage->InitialStack, DetailLevel.InsertionState.EventStack, LastPage->InitialStackCount * sizeof(FEventStackEntry));
			}
		}
		LastPage->EndTime = Time;

		if (IsEnter)
		{
			FEventStackEntry& StackEntry = DetailLevel.InsertionState.EventStack[DetailLevel.InsertionState.CurrentDepth++];
			check(DetailLevel.InsertionState.CurrentDepth < SettingsType::MaxDepth);
			StackEntry.EventIndex = EventIndex;
			StackEntry.EnterScopeIndex = ScopeIndex;
		}
		else
		{
			check(DetailLevel.InsertionState.CurrentDepth > 0);
			--DetailLevel.InsertionState.CurrentDepth;
		}

		//ScopeEntry.DebugDepth = DetailLevel.InsertionState.CurrentDepth;
	}

	void AddEvent(FDetailLevel& DetailLevel, const EventType& Event)
	{
		++DetailLevel.InsertionState.CurrentScopeEntryPage->EndEventIndex;
		DetailLevel.Events.PushBack() = Event;

		//Event.DebugType = Owner.EventTypes[TypeIndex];
	}

	double GetScopeEntryTime(const FDetailLevel& DetailLevel, uint64 Index) const
	{
		const FEventScopeEntry& ScopeEntry = DetailLevel.ScopeEntries[Index];
		return ScopeEntry.Time < 0 ? -ScopeEntry.Time : ScopeEntry.Time;
	}

	void SetEvent(FDetailLevel& DetailLevel, uint64 Index, const EventType& Event)
	{
		DetailLevel.Events[Index] = Event;
	}

	EventType GetEvent(const FDetailLevel& DetailLevel, uint64 Index) const
	{
		return DetailLevel.Events[Index];
	}

	ILinearAllocator& Allocator;
	TArray<FDetailLevel> DetailLevels;
	uint64 ModCount = 0;
};

}