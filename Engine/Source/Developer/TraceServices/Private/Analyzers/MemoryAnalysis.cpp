// Copyright Epic Games, Inc. All Rights Reserved.
#include "MemoryAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"

FMemoryAnalyzer::FMemoryAnalyzer(Trace::IAnalysisSession& InSession)
	: Provider(nullptr)
	, Session(InSession)
{
	Provider = Session.EditProvider<Trace::FMemoryProvider>(Trace::FMemoryProvider::ProviderName);
}

FMemoryAnalyzer::~FMemoryAnalyzer()
{
}

void FMemoryAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_TagsSpec, "LLM", "TagsSpec");
	Builder.RouteEvent(RouteId_TrackerSpec, "LLM", "TrackerSpec");
	Builder.RouteEvent(RouteId_TagValue, "LLM", "TagValue");
}

bool FMemoryAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	if (!Provider)
	{
		return false;
	}

	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_TagsSpec:
	{
		FString Name = FTraceAnalyzerUtils::LegacyAttachmentString<TCHAR>("Name", Context);
		const int64 TagId = EventData.GetValue<int64>("TagId");
		const int64 ParentId = EventData.GetValue<int64>("ParentId");
		Provider->AddEventSpec(TagId, *Name, ParentId);
	}
	break;
	case RouteId_TrackerSpec:
	{
		uint8 TrackerId = EventData.GetValue<uint8>("TrackerId");
		FString Name = FTraceAnalyzerUtils::LegacyAttachmentString<ANSICHAR>("Name", Context);
		Provider->AddTrackerSpec(TrackerId, *Name);
	}
	break;
	case RouteId_TagValue:
	{
		const uint8 TrackerId = EventData.GetValue<uint8>("TrackerId");
		const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		const double Time = Context.EventTime.AsSeconds(Cycle);
		const TArrayReader<int64>& Tags = EventData.GetArray<int64>("Tags");
		const TArrayReader<int64>& Samples = EventData.GetArray<int64>("Values");

		const uint32 TagsCount = Tags.Num();
		TArray<Trace::FMemoryTagSample> Values;
		for (uint32 i = 0; i < TagsCount; ++i)
		{
			Values.Push(Trace::FMemoryTagSample{ Samples[i] });
		}
		Provider->AddTagSnapshot(TrackerId, Time, Tags.GetData(), Values.GetData(), TagsCount);
		Sample++;

		Session.UpdateDurationSeconds(Time);
	}
	break;
	}
	return true;
}
