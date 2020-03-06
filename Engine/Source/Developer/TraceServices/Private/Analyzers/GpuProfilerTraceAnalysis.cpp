// Copyright Epic Games, Inc. All Rights Reserved.
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

bool FGpuProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;

	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint32 EventType = EventData.GetValue<uint32>("EventType");
		const auto& Name = EventData.GetArray<uint16>("Name");

		FString EventName(Name.GetData(), Name.Num());
		EventTypeMap.Add(EventType, TimingProfilerProvider.AddGpuTimer(*EventName));
		break;
	}
	case RouteId_Frame:
	{
		const auto& Data = EventData.GetArray<uint8>("Data");
		const uint8* BufferPtr = Data.GetData();
		const uint8* BufferEnd = BufferPtr + Data.Num();

		uint32 CurrentDepth = 0;

		uint64 CalibrationBias = EventData.GetValue<uint64>("CalibrationBias");
		uint64 LastTimestamp = EventData.GetValue<uint64>("TimestampBase");
		double LastTime = 0.0;
		while (BufferPtr < BufferEnd)
		{
			uint64 DecodedTimestamp = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 ActualTimestamp = (DecodedTimestamp >> 1) + LastTimestamp;
			LastTimestamp = ActualTimestamp;
			LastTime = double(ActualTimestamp + CalibrationBias) / 1000000.0;
			LastTime -= Context.SessionContext.StartCycle / (double)Context.SessionContext.CycleFrequency;

			// The monolithic timeline assumes that timestamps are ever increasing, but
			// with gpu/cpu calibration and drift there can be a tiny bit of overlap between
			// frames. So we just clamp.
			if (MinTime > LastTime)
			{
				LastTime = MinTime;
			}
			MinTime = LastTime;

			if (DecodedTimestamp & 1ull)
			{
				uint32 EventType = *reinterpret_cast<const uint32*>(BufferPtr);
				BufferPtr += sizeof(uint32);
				if (EventTypeMap.Contains(EventType))
				{
					Trace::FTimingProfilerEvent Event;
					Event.TimerIndex = EventTypeMap[EventType];
					Timeline.AppendBeginEvent(LastTime, Event);
				}
				++CurrentDepth;
			}
			else
			{
				if (CurrentDepth > 0)
				{
					--CurrentDepth;
				}
				Timeline.AppendEndEvent(LastTime);
			}
		}
		Session.UpdateDurationSeconds(LastTime);
		check(BufferPtr == BufferEnd);
		check(CurrentDepth == 0);
		break;
	}
		
	}

	return true;
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
