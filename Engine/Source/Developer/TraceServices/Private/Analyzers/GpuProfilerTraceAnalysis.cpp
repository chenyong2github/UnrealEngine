// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "GpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"

FGpuProfilerAnalyzer::FGpuProfilerAnalyzer(Trace::FAnalysisSession& InSession, Trace::FTimingProfilerProvider& InTimingProfilerProvider)
	: Session(InSession)
	, TimingProfilerProvider(InTimingProfilerProvider)
	, Timeline(TimingProfilerProvider.EditGpuTimeline())
	, Calibrated(false)
{

}

void FGpuProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_EventSpec, "GpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_Frame, "GpuProfiler", "Frame");
}

void FGpuProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;

	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint64 EventType = EventData.GetValue("EventType").As<uint64>();
		FString EventName(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()), EventData.GetValue("NameLength").As<uint16>());
		EventTypeMap.Add(EventType, TimingProfilerProvider.AddGpuTimer(*EventName));
		break;
	}
	case RouteId_Frame:
	{
		uint64 BufferSize = EventData.GetAttachmentSize();
		const uint8* BufferPtr = EventData.GetAttachment();
		const uint8* BufferEnd = BufferPtr + BufferSize;

		uint32 CurrentDepth = 0;

		uint64 LastTimestamp = EventData.GetValue("TimestampBase").As<uint64>();
		double LastTime = 0.0;
		while (BufferPtr < BufferEnd)
		{
			uint64 DecodedTimestamp = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 ActualTimestamp = (DecodedTimestamp >> 1) + LastTimestamp;
			LastTimestamp = ActualTimestamp;
			LastTime = GpuTimestampToSessionTime(ActualTimestamp);
			if (DecodedTimestamp & 1ull)
			{
				uint64 EventType = *reinterpret_cast<const uint64*>(BufferPtr);
				BufferPtr += sizeof(uint64);
				check(EventTypeMap.Contains(EventType));
				Trace::FTimingProfilerEvent Event;
				Event.TimerIndex = EventTypeMap[EventType];
				Timeline.AppendBeginEvent(LastTime, Event);
				++CurrentDepth;
			}
			else
			{
				check(CurrentDepth > 0);
				--CurrentDepth;
				Timeline.AppendEndEvent(LastTime);
			}
		}
		Session.UpdateDurationSeconds(LastTime);
		check(BufferPtr == BufferEnd);
		check(CurrentDepth == 0);
		break;
	}
		
	}
}

double FGpuProfilerAnalyzer::GpuTimestampToSessionTime(uint64 GpuMicroseconds)
{
	if (!Calibrated)
	{
		uint64 SessionTimeMicroseconds = uint64(Session.GetDurationSeconds() * 1000000.0);
		GpuTimeOffset = GpuMicroseconds - SessionTimeMicroseconds;
		Calibrated = true;
	}
	return (GpuMicroseconds - GpuTimeOffset) / 1000000.0;
}
