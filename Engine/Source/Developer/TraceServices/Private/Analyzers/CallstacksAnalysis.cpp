// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallstacksAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/CallstacksProvider.h"

////////////////////////////////////////////////////////////////////////////////
FCallstacksAnalyzer::FCallstacksAnalyzer(Trace::IAnalysisSession& Session, Trace::FCallstacksProvider* InProvider)
	: Provider(InProvider)
{
}

////////////////////////////////////////////////////////////////////////////////
void FCallstacksAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_Callstack, "Memory", "CallstackSpec");
}


////////////////////////////////////////////////////////////////////////////////
bool FCallstacksAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	switch(RouteId)
	{
		case RouteId_Callstack:
			const uint64 Id = Context.EventData.GetValue<uint64>("Id");
			const TArrayReader<uint64>& Frames = Context.EventData.GetArray<uint64>("Frames");
			Provider->AddCallstack(Id, Frames.GetData(), uint8(Frames.Num()));
			break;
	}
	return true;
}

