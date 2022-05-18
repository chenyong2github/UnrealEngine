// Copyright Epic Games, Inc. All Rights Reserved.

#include "StatsTraceAnalysis.h"

#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "HAL/LowLevelMemTracker.h"
#include "TraceServices/Model/Counters.h"

#include <limits>

#define STATS_ANALYZER_DEBUG_LOG(StatId, Format, ...) //{ if (StatId == 389078 /*STAT_MeshDrawCalls*/) UE_LOG(LogTraceServices, Log, Format, ##__VA_ARGS__); }

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
	Builder.RouteEvent(RouteId_BeginFrame, "Misc", "BeginFrame");
	Builder.RouteEvent(RouteId_EndFrame, "Misc", "EndFrame");
}

void FStatsAnalyzer::OnAnalysisEnd()
{
	CreateFrameCounters();
}

bool FStatsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FStatsAnalyzer"));

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
			FString Group;
			if (EventData.GetString("Name", Name))
			{
				EventData.GetString("Description", Description);
				EventData.GetString("Group", Group);
			}
			else
			{
				Name = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
				Description = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + Name.Len() + 1);
			}

			if (Name.IsEmpty())
			{
				UE_LOG(LogTraceServices, Warning, TEXT("Invalid counter name for Stats counter %u."), StatId);
				Name = FString::Printf(TEXT("<noname stats counter %u>"), StatId);
			}
			Counter->SetName(Session.StoreString(*Name));

			if (!Group.IsEmpty())
			{
				Counter->SetGroup(Session.StoreString(*Group));
			}
			Counter->SetDescription(Session.StoreString(*Description));

			Counter->SetIsFloatingPoint(EventData.GetValue<bool>("IsFloatingPoint"));
			Counter->SetIsResetEveryFrame(EventData.GetValue<bool>("ShouldClearEveryFrame"));

			ECounterDisplayHint DisplayHint = CounterDisplayHint_None;
			if (EventData.GetValue<bool>("IsMemory"))
			{
				DisplayHint = CounterDisplayHint_Memory;
			}
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
					FString Name = FString::Printf(TEXT("<unknown stats counter %u>"), StatId);
					Counter->SetName(Session.StoreString(*Name));
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
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f INC() %u"), Time, ThreadId);
					break;
				}
				case Decrement:
				{
					Counter->AddValue(Time, int64(-1));
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f DEC() %u"), Time, ThreadId);
					break;
				}
				case AddInteger:
				{
					int64 Amount = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
					Counter->AddValue(Time, Amount);
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f ADD(%lli) %u"), Time, Amount, ThreadId);
					break;
				}
				case SetInteger:
				{
					int64 Value = FTraceAnalyzerUtils::DecodeZigZag(BufferPtr);
					Counter->SetValue(Time, Value);
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f SET(%lli) %u"), Time, Value, ThreadId);
					break;
				}
				case AddFloat:
				{
					double Amount;
					memcpy(&Amount, BufferPtr, sizeof(double));
					BufferPtr += sizeof(double);
					Counter->AddValue(Time, Amount);
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f ADD(%f) %u"), Time, Amount, ThreadId);
					break;
				}
				case SetFloat:
				{
					double Value;
					memcpy(&Value, BufferPtr, sizeof(double));
					BufferPtr += sizeof(double);
					Counter->SetValue(Time, Value);
					STATS_ANALYZER_DEBUG_LOG(StatId, TEXT("%f SET(%f) %u"), Time, Value, ThreadId);
					break;
				}
				}
			}
			check(BufferPtr == BufferEnd);
			break;
		}

		case RouteId_BeginFrame:
		//case RouteId_EndFrame:
		{
			uint8 FrameType = EventData.GetValue<uint8>("FrameType");
			check(FrameType < TraceFrameType_Count);
			if (ETraceFrameType(FrameType) == ETraceFrameType::TraceFrameType_Game)
			{
				uint64 Cycle = EventData.GetValue<uint64>("Cycle");
				double Time = Context.EventTime.AsSeconds(Cycle);
				for (auto& KV : CountersMap)
				{
					if (KV.Value->IsResetEveryFrame())
					{
						STATS_ANALYZER_DEBUG_LOG(KV.Key, TEXT("%f RESET"), Time);
						if (KV.Value->IsFloatingPoint())
						{
							KV.Value->SetValue(Time, 0.0);
						}
						else
						{
							KV.Value->SetValue(Time, (int64)0);
						}
					}
				}
			}
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

void FStatsAnalyzer::CreateFrameCounters()
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FStatsAnalyzer"));

	FAnalysisSessionEditScope _(Session);

	TMap<uint32, IEditableCounter*> ResetEveryFrameCountersMap;

	for (auto& KV : CountersMap)
	{
		uint32 StatId = KV.Key;
		IEditableCounter* Counter = KV.Value;

		if (Counter->IsResetEveryFrame())
		{
			ResetEveryFrameCountersMap.Add(StatId, Counter);
		}
	}

	for (auto& KV : ResetEveryFrameCountersMap)
	{
		IEditableCounter* Counter = KV.Value;
		IEditableCounter* FrameCounter = CounterProvider.CreateCounter();

		FString FrameCounterName = FString(Counter->GetName()) + TEXT(" (1/frame)");
		FrameCounter->SetName(Session.StoreString(*FrameCounterName));
		if (Counter->GetGroup())
		{
			FrameCounter->SetGroup(Counter->GetGroup());
		}
		if (Counter->GetDescription())
		{
			FrameCounter->SetDescription(Counter->GetDescription());
		}

		FrameCounter->SetIsFloatingPoint(Counter->IsFloatingPoint());
		FrameCounter->SetIsResetEveryFrame(false);
		FrameCounter->SetDisplayHint(Counter->GetDisplayHint());

		constexpr double InfiniteTime = std::numeric_limits<double>::infinity();

		if (Counter->IsFloatingPoint())
		{
			bool bFirst = true;
			double FrameTime = 0.0;
			double FrameValue = 0.0;
			Counter->EnumerateFloatValues(-InfiniteTime, InfiniteTime, false, [FrameCounter, &bFirst, &FrameTime, &FrameValue](double Time, double Value)
				{
					if (bFirst && Value != 0.0)
					{
						bFirst = false;
						FrameCounter->SetValue(Time, 0.0);
					}
					if (Value == 0.0 && FrameValue != 0.0)
					{
						FrameCounter->SetValue(FrameTime, FrameValue);
					}
					FrameTime = Time;
					FrameValue = Value;
				});
		}
		else
		{
			bool bFirst = true;
			double FrameTime = 0.0;
			int64 FrameValue = 0;
			Counter->EnumerateValues(-InfiniteTime, InfiniteTime, false, [FrameCounter, &bFirst, &FrameTime, &FrameValue](double Time, int64 Value)
				{
					if (bFirst && Value != 0)
					{
						bFirst = false;
						FrameCounter->SetValue(Time, (int64)0);
					}
					if (Value == 0 && FrameValue != 0)
					{
						FrameCounter->SetValue(FrameTime, FrameValue);
					}
					FrameTime = Time;
					FrameValue = Value;
				});
		}
	}
}

} // namespace TraceServices

#undef STATS_ANALYZER_DEBUG_LOG
