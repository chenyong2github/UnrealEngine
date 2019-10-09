// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "StatsTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "TraceServices/Model/Counters.h"

FStatsAnalyzer::FStatsAnalyzer(Trace::IAnalysisSession& InSession, Trace::ICounterProvider& InCounterProvider)
	: Session(InSession)
	, CounterProvider(InCounterProvider)
{

}

void FStatsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Spec, "Stats", "Spec");
	Builder.RouteEvent(RouteId_EventBatch, "Stats", "EventBatch");
}

bool FStatsAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Spec:
	{
		uint32 StatId = EventData.GetValue<uint32>("Id");
		Trace::IEditableCounter* Counter = CountersMap.FindRef(StatId);
		if (!Counter)
		{
			Counter = CounterProvider.CreateCounter();
			CountersMap.Add(StatId, Counter);
		}
		const ANSICHAR* Name = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		const TCHAR* Description = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(Name) + 1);
		Trace::ECounterDisplayHint DisplayHint = Trace::CounterDisplayHint_None;
		if (EventData.GetValue<bool>("IsMemory"))
		{
			DisplayHint = Trace::CounterDisplayHint_Memory;
		}
		Counter->SetName(Session.StoreString(ANSI_TO_TCHAR(Name)));
		Counter->SetDescription(Session.StoreString(Description));
		Counter->SetIsFloatingPoint(EventData.GetValue<bool>("IsFloatingPoint"));
		Counter->SetDisplayHint(DisplayHint);
		break;
	}
	case RouteId_EventBatch:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 BufferSize = EventData.GetAttachmentSize();
		const uint8* BufferPtr = EventData.GetAttachment();
		const uint8* BufferEnd = BufferPtr + BufferSize;
		while (BufferPtr < BufferEnd)
		{
			enum EOpType
			{
				Increment = 0,
				Decrement = 1,
				AddInteger = 2,
				SetInteger = 3,
				AddFloat = 4,
				SetFloat = 5,
			};

			uint64 DecodedIdAndOp = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint32 StatId = DecodedIdAndOp >> 3;
			Trace::IEditableCounter* Counter = CountersMap.FindRef(StatId);
			if (!Counter)
			{
				Counter = CounterProvider.CreateCounter();
				CountersMap.Add(StatId, Counter);
			}
			uint8 Op = DecodedIdAndOp & 0x7;
			uint64 CycleDiff = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 Cycle = ThreadState->LastCycle + CycleDiff;
			double Time = Context.SessionContext.TimestampFromCycle(Cycle);
			ThreadState->LastCycle = Cycle;
			switch (Op)
			{
			case Increment:
			{
				Counter->AddValue(Time, int64(1));
				break;
			}
			case Decrement:
			{
				Counter->AddValue(Time, int64(-1));
				break;
			}
			case AddInteger:
			{
				int64 Amount = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
				Counter->AddValue(Time, Amount);
				break;
			}
			case SetInteger:
			{
				int64 Value = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
				Counter->SetValue(Time, Value);
				break;
			}
			case AddFloat:
			{
				double Amount;
				memcpy(&Amount, BufferPtr, sizeof(double));
				BufferPtr += sizeof(double);
				Counter->AddValue(Time, Amount);
				break;
			}
			case SetFloat:
			{
				double Value;
				memcpy(&Value, BufferPtr, sizeof(double));
				BufferPtr += sizeof(double);
				Counter->SetValue(Time, Value);
				break;
			}
			}
		}
		check(BufferPtr == BufferEnd);
		break;
	}
	}

	return true;
}

TSharedRef<FStatsAnalyzer::FThreadState> FStatsAnalyzer::GetThreadState(uint32 ThreadId)
{
	if (!ThreadStatesMap.Contains(ThreadId))
	{
		TSharedRef<FThreadState> ThreadState = MakeShared<FThreadState>();
		ThreadState->LastCycle = 0;
		ThreadStatesMap.Add(ThreadId, ThreadState);
		return ThreadState;
	}
	else
	{
		return ThreadStatesMap[ThreadId];
	}
}
