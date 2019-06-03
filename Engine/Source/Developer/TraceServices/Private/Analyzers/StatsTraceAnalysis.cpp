// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "StatsTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Model/Counters.h"

FStatsAnalyzer::FStatsAnalyzer(Trace::FAnalysisSession& InSession, Trace::FCounterProvider& InCounterProvider)
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

void FStatsAnalyzer::OnAnalysisEnd()
{
}

void FStatsAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Spec:
	{
		Trace::FAnalysisSessionEditScope _(Session);
		uint32 StatId = EventData.GetValue("Id").As<uint32>();
		check(!CountersMap.Contains(StatId));
		const ANSICHAR* Name = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		const TCHAR* Description = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(Name) + 1);
		Trace::ECounterDisplayHint DisplayHint = Trace::CounterDisplayHint_None;
		if (EventData.GetValue("IsFloatingPoint").As<bool>())
		{
			DisplayHint = Trace::CounterDisplayHint_FloatingPoint;
		}
		else if (EventData.GetValue("IsMemory").As<bool>())
		{
			DisplayHint = Trace::CounterDisplayHint_Memory;
		}
		Trace::FCounterInternal* Counter = CounterProvider.CreateCounter(ANSI_TO_TCHAR(Name), Description, DisplayHint);
		CountersMap.Add(StatId, Counter);
		break;
	}
	case RouteId_EventBatch:
	{
		Trace::FAnalysisSessionEditScope _(Session);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
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
			//check(CountersMap.Contains(StatId));
			Trace::FCounterInternal& Counter = *CountersMap[StatId];
			uint8 Op = DecodedIdAndOp & 0x7;
			uint64 CycleDiff = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 Cycle = ThreadState->LastCycle + CycleDiff;
			double Time = Context.SessionContext.TimestampFromCycle(Cycle);
			ThreadState->LastCycle = Cycle;
			switch (Op)
			{
			case Increment:
			{
				CounterProvider.Add(Counter, Time, int64(1));
				break;
			}
			case Decrement:
			{
				CounterProvider.Add(Counter, Time, int64(-1));
				break;
			}
			case AddInteger:
			{
				int64 Amount = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
				CounterProvider.Add(Counter, Time, Amount);
				break;
			}
			case SetInteger:
			{
				int64 Value = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
				CounterProvider.Set(Counter, Time, Value);
				break;
			}
			case AddFloat:
			{
				double Amount;
				memcpy(&Amount, BufferPtr, sizeof(double));
				BufferPtr += sizeof(double);
				CounterProvider.Add(Counter, Time, Amount);
				break;
			}
			case SetFloat:
			{
				double Value;
				memcpy(&Value, BufferPtr, sizeof(double));
				BufferPtr += sizeof(double);
				break;
			}
			}
		}
		check(BufferPtr == BufferEnd);
		break;
	}
	}
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
