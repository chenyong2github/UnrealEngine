// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CountersTraceAnalysis.h"
#include "TraceServices/Model/Counters.h"
#include "ProfilingDebugging/CountersTrace.h"

FCountersAnalyzer::FCountersAnalyzer(Trace::IAnalysisSession& InSession, Trace::ICounterProvider& InCounterProvider)
	: Session(InSession)
	, CounterProvider(InCounterProvider)
{

}

void FCountersAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Spec, "Counters", "Spec");
	Builder.RouteEvent(RouteId_SetValueInt, "Counters", "SetValueInt");
	Builder.RouteEvent(RouteId_SetValueFloat, "Counters", "SetValueFloat");
}

bool FCountersAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Spec:
	{
		uint16 CounterId = EventData.GetValue<uint16>("Id");
		ETraceCounterType CounterType = static_cast<ETraceCounterType>(EventData.GetValue<uint8>("Type"));
		ETraceCounterDisplayHint CounterDisplayHint = static_cast<ETraceCounterDisplayHint>(EventData.GetValue<uint8>("DisplayHint"));
		Trace::IEditableCounter* Counter = CounterProvider.CreateCounter();
		if (CounterType == TraceCounterType_Float)
		{
			Counter->SetIsFloatingPoint(true);
		}
		if (CounterDisplayHint == TraceCounterDisplayHint_Memory)
		{
			Counter->SetDisplayHint(Trace::CounterDisplayHint_Memory);
		}
		Counter->SetName(Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment())));
		CountersMap.Add(CounterId, Counter);
		break;
	}
	case RouteId_SetValueInt:
	{
		uint16 CounterId = EventData.GetValue<uint16>("CounterId");
		int64 Value = EventData.GetValue<int64>("Value");
		double Timestamp = Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle"));
		Trace::IEditableCounter** FindCounter = CountersMap.Find(CounterId);
		if (FindCounter)
		{
			(*FindCounter)->SetValue(Timestamp, Value);
		}
		break;
	}
	case RouteId_SetValueFloat:
	{
		uint16 CounterId = EventData.GetValue<uint16>("CounterId");
		float Value = EventData.GetValue<float>("Value");
		double Timestamp = Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle"));
		Trace::IEditableCounter* FindCounter = CountersMap.FindRef(CounterId);
		if (FindCounter)
		{
			FindCounter->SetValue(Timestamp, Value);
		}
		break;
	}
	}

	return true;
}
