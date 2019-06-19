// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CsvProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/Model/Counters.h"

FCsvProfilerAnalyzer::FCsvProfilerAnalyzer(Trace::IAnalysisSession& InSession, Trace::ICounterProvider& InCounterProvider)
	: Session(InSession)
	, CounterProvider(InCounterProvider)
{

}

void FCsvProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_InlineStat, "CsvProfiler", "InlineStat");
	Builder.RouteEvent(RouteId_DeclaredStat, "CsvProfiler", "DeclaredStat");
	Builder.RouteEvent(RouteId_CustomStatInlineInt, "CsvProfiler", "CustomStatInlineInt");
	Builder.RouteEvent(RouteId_CustomStatInlineFloat, "CsvProfiler", "CustomStatInlineFloat");
	Builder.RouteEvent(RouteId_CustomStatDeclaredInt, "CsvProfiler", "CustomStatDeclaredInt");
	Builder.RouteEvent(RouteId_CustomStatDeclaredFloat, "CsvProfiler", "CustomStatDeclaredFloat");
}

void FCsvProfilerAnalyzer::OnAnalysisEnd()
{
}

void FCsvProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_InlineStat:
	{
		uint64 StatNamePointer = EventData.GetValue<uint64>("StatNamePointer");
		InlinePendingCountersMap.Add(StatNamePointer, Session.StoreString(ANSI_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment()))));
		break;
	}
	case RouteId_DeclaredStat:
	{
		uint32 StatId = EventData.GetValue<uint32>("StatId");
		DeclaredPendingCountersMap.Add(StatId, Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment())));
		break;
	}
	case RouteId_CustomStatInlineInt:
	{
		Trace::ICounter* Counter = GetInlineStatCounter(EventData.GetValue<uint64>("StatNamePointer"), false);
		HandleIntCounterEvent(Counter, Context);
		break;
	}
	case RouteId_CustomStatInlineFloat:
	{
		Trace::ICounter* Counter = GetInlineStatCounter(EventData.GetValue<uint64>("StatNamePointer"), true);
		HandleFloatCounterEvent(Counter, Context);
		break;
	}
	case RouteId_CustomStatDeclaredInt:
	{
		Trace::ICounter* Counter = GetDeclaredStatCounter(EventData.GetValue<uint32>("StatId"), false);
		HandleIntCounterEvent(Counter, Context);
		break;
	}
	case RouteId_CustomStatDeclaredFloat:
	{
		Trace::ICounter* Counter = GetDeclaredStatCounter(EventData.GetValue<uint32>("StatId"), true);
		HandleFloatCounterEvent(Counter, Context);
		break;
	}
	}
}

void FCsvProfilerAnalyzer::HandleIntCounterEvent(Trace::ICounter* Counter, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	double Timestamp = Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle"));
	uint8 OpType = EventData.GetValue<uint8>("OpType");
	if (OpType == OpType_Accumulate)
	{
		Counter->AddValue(Timestamp, int64(EventData.GetValue<int32>("Value")));
	}
	else
	{
		Counter->SetValue(Timestamp, int64(EventData.GetValue<int32>("Value")));
	}
}

void FCsvProfilerAnalyzer::HandleFloatCounterEvent(Trace::ICounter* Counter, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	double Timestamp = Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle"));
	uint8 OpType = EventData.GetValue<uint8>("OpType");
	if (OpType == OpType_Accumulate)
	{
		Counter->AddValue(Timestamp, double(EventData.GetValue<float>("Value")));
	}
	else
	{
		Counter->SetValue(Timestamp, double(EventData.GetValue<float>("Value")));
	}
}

Trace::ICounter* FCsvProfilerAnalyzer::GetDeclaredStatCounter(uint32 StatId, bool bIsFloatingPoint)
{
	Trace::ICounter** FindIt;
	FindIt = DeclaredCountersMap.Find(StatId);
	if (!FindIt)
	{
		Trace::ICounter* Counter = CounterProvider.CreateCounter();
		Counter->SetIsFloatingPoint(bIsFloatingPoint);
		Counter->SetIsResetEveryFrame(true);
		const TCHAR** FindName = DeclaredPendingCountersMap.Find(StatId);
		if (FindName)
		{
			Counter->SetName(*FindName);
			DeclaredPendingCountersMap.Remove(StatId);
		}
		DeclaredCountersMap.Add(StatId, Counter);
		return Counter;
	}
	else
	{
		return *FindIt;
	}
}

Trace::ICounter* FCsvProfilerAnalyzer::GetInlineStatCounter(uint64 StatNamePointer, bool bIsFloatingPoint)
{
	Trace::ICounter** FindIt;
	FindIt = InlineCountersMap.Find(StatNamePointer);
	if (!FindIt)
	{
		Trace::ICounter* Counter = CounterProvider.CreateCounter();
		Counter->SetIsFloatingPoint(bIsFloatingPoint);
		Counter->SetIsResetEveryFrame(true);
		InlineCountersMap.Add(StatNamePointer, Counter);
		const TCHAR** FindName = InlinePendingCountersMap.Find(StatNamePointer);
		if (FindName)
		{
			Counter->SetName(*FindName);
			InlinePendingCountersMap.Remove(StatNamePointer);
		}
		return Counter;
	}
	else
	{
		return *FindIt;
	}
}
