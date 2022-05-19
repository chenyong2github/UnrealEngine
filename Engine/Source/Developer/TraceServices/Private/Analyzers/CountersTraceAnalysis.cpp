// Copyright Epic Games, Inc. All Rights Reserved.

#include "CountersTraceAnalysis.h"

#include "Common/Utils.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "TraceServices/Model/Counters.h"

namespace TraceServices
{

FCountersAnalyzer::FCountersAnalyzer(IAnalysisSession& InSession, ICounterProvider& InCounterProvider)
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

bool FCountersAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FCountersAnalyzer"));

	FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Spec:
	{
		uint16 CounterId = EventData.GetValue<uint16>("Id");
		ETraceCounterType CounterType = static_cast<ETraceCounterType>(EventData.GetValue<uint8>("Type"));
		ETraceCounterDisplayHint CounterDisplayHint = static_cast<ETraceCounterDisplayHint>(EventData.GetValue<uint8>("DisplayHint"));
		IEditableCounter* Counter = CounterProvider.CreateCounter();
		if (CounterType == TraceCounterType_Float)
		{
			Counter->SetIsFloatingPoint(true);
		}
		if (CounterDisplayHint == TraceCounterDisplayHint_Memory)
		{
			Counter->SetDisplayHint(CounterDisplayHint_Memory);
		}
		FString Name = FTraceAnalyzerUtils::LegacyAttachmentString<TCHAR>("Name", Context);
		if (Name.IsEmpty())
		{
			UE_LOG(LogTraceServices, Warning, TEXT("Invalid counter name for counter %u."), uint32(CounterId));
			Name = FString::Printf(TEXT("<noname counter %u>"), uint32(CounterId));
		}
		Counter->SetName(Session.StoreString(*Name));
		CountersMap.Add(CounterId, Counter);
		break;
	}
	case RouteId_SetValueInt:
	{
		uint16 CounterId = EventData.GetValue<uint16>("CounterId");
		int64 Value = EventData.GetValue<int64>("Value");
		double Timestamp = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		IEditableCounter* FindCounter = CountersMap.FindRef(CounterId);
		if (ensure(FindCounter))
		{
			FindCounter->SetValue(Timestamp, Value);
		}
		break;
	}
	case RouteId_SetValueFloat:
	{
		uint16 CounterId = EventData.GetValue<uint16>("CounterId");
		float Value = EventData.GetValue<float>("Value");
		double Timestamp = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		IEditableCounter* FindCounter = CountersMap.FindRef(CounterId);
		if (ensure(FindCounter))
		{
			FindCounter->SetValue(Timestamp, Value);
		}
		break;
	}
	}

	return true;
}

} // namespace TraceServices
