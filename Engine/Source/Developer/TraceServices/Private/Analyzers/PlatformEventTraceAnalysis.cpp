// Copyright Epic Games, Inc. All Rights Reserved.
#include "PlatformEventTraceAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/ContextSwitchesPrivate.h"
#include "Model/StackSamplesPrivate.h"

namespace TraceServices
{

FPlatformEventTraceAnalyzer::FPlatformEventTraceAnalyzer(IAnalysisSession& InSession,
														 FContextSwitchProvider& ContextSwitchProvider,
														 FStackSampleProvider& StackSampleProvider)
	: Session(InSession)
	, ContextSwitchProvider(ContextSwitchProvider)
	, StackSampleProvider(StackSampleProvider)
{
}

void FPlatformEventTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_ContextSwitch, "PlatformEvent", "ContextSwitch");
	Builder.RouteEvent(RouteId_StackSample, "PlatformEvent", "StackSample");
}

bool FPlatformEventTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{

	case RouteId_ContextSwitch:
	{
		double Start = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("StartTime"));
		double End = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("EndTime"));
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		uint32 CoreNumber = EventData.GetValue<uint8>("CoreNumber");
		ContextSwitchProvider.Add(ThreadId, Start, End, CoreNumber);

		Session.UpdateDurationSeconds(End);

		break;
	}

	case RouteId_StackSample:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Time"));
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		const TArrayReader<uint64>& Addresses = EventData.GetArray<uint64>("Addresses");
		StackSampleProvider.Add(ThreadId, Time, Addresses.Num(), Addresses.GetData());

		Session.UpdateDurationSeconds(Time);

		break;
	}

	}

	return true;
}

void FPlatformEventTraceAnalyzer::OnThreadInfo(const FThreadInfo& ThreadInfo)
{
	FAnalysisSessionEditScope _(Session);
	ContextSwitchProvider.AddThreadInfo(ThreadInfo.GetId(), ThreadInfo.GetSystemId());
}

} // namespace TraceServices
