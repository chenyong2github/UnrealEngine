// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallstacksAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/CallstacksProvider.h"

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////
FCallstacksAnalyzer::FCallstacksAnalyzer(IAnalysisSession& InSession, FCallstacksProvider* InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
	check(Provider != nullptr);
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
			const TArrayReader<uint64>& Frames = Context.EventData.GetArray<uint64>("Frames");
			if (const uint64 Hash = Context.EventData.GetValue<uint64>("Id"))
			{
				Provider->AddCallstack(Hash, Frames.GetData(), uint8(Frames.Num()));
			}
			else if (const uint32 Id = Context.EventData.GetValue<uint32>("CallstackId"))
			{
				Provider->AddCallstack(Id, Frames.GetData(), uint8(Frames.Num()));
			}
			break;
	}
	return true;
}

} // namespace TraceServices
