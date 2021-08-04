// Copyright Epic Games, Inc. All Rights Reserved.
#include "StatsTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "TraceServices/Model/Counters.h"

namespace TraceServices
{

FStatsAnalyzer::FStatsAnalyzer(IAnalysisSession& InSession, ICounterProvider& InCounterProvider)
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

bool FStatsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Spec:
	{
		uint32 StatId = EventData.GetValue<uint32>("Id");
		IEditableCounter* Counter = CountersMap.FindRef(StatId);
		if (!Counter)
		{
			Counter = CounterProvider.CreateCounter();
			CountersMap.Add(StatId, Counter);
		}

		FString Name;
		FString Description;
		if (EventData.GetString("Name", Name))
		{
			EventData.GetString("Description", Description);
		}
		else
		{
			Name = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
			Description = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + Name.Len() + 1);
		}


		ECounterDisplayHint DisplayHint = CounterDisplayHint_None;
		if (EventData.GetValue<bool>("IsMemory"))
		{
			DisplayHint = CounterDisplayHint_Memory;
		}
		Counter->SetName(Session.StoreString(*Name));
		Counter->SetDescription(Session.StoreString(*Description));
		Counter->SetIsFloatingPoint(EventData.GetValue<bool>("IsFloatingPoint"));
		Counter->SetDisplayHint(DisplayHint);
		break;
	}
	case RouteId_EventBatch:
	{
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		TArrayView<const uint8> DataView = FTraceAnalyzerUtils::LegacyAttachmentArray("Data", Context);
		uint64 BufferSize = DataView.Num();
		const uint8* BufferPtr = DataView.GetData();
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
			IEditableCounter* Counter = CountersMap.FindRef(StatId);
			if (!Counter)
			{
				Counter = CounterProvider.CreateCounter();
				CountersMap.Add(StatId, Counter);
			}
			uint8 Op = DecodedIdAndOp & 0x7;
			uint64 CycleDiff = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 Cycle = ThreadState->LastCycle + CycleDiff;
			double Time = Context.EventTime.AsSeconds(Cycle);
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

} // namespace TraceServices
