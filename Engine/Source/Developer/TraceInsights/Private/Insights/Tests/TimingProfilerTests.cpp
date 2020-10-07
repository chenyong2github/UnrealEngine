// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/Tests/TimingProfilerTests.h"

#include "Insights/Common/Stopwatch.h"

#include "TraceServices/Model/TimingProfiler.h"
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(TimingProfilerTests);

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerTests::RunEnumerateBenchmark(const FEnumerateTestParams& InParams, FCheckValues& OutCheckValues)
{
	UE_LOG(TimingProfilerTests, Log, TEXT("RUNNING BENCHMARK..."));

	FStopwatch Stopwatch;
	Stopwatch.Start();

	double SessionTime = 0.0;
	uint32 TimelineIndex = (uint32)-1;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		SessionTime = Session->GetDurationSeconds();
		OutCheckValues.SessionDuration = SessionTime;

		const double TimeIncrement = SessionTime / static_cast<double>(InParams.NumEnumerations);

		const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(*Session.Get());
		ThreadProvider.EnumerateThreads(
			[&TimelineIndex, &TimingProfilerProvider](const Trace::FThreadInfo& ThreadInfo)
			{
				if (!FCString::Strcmp(ThreadInfo.Name, TEXT("GameThread")))
				{
					TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, TimelineIndex);
				}
			});

		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[&OutCheckValues, &InParams, TimeIncrement](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				double Time = 0.0;
				for (int32 Index = 0; Index < InParams.NumEnumerations; ++Index)
				{
					Timeline.EnumerateEvents(Time, Time + InParams.Interval,
						[&OutCheckValues](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
						{
							OutCheckValues.TotalEventDuration += EventEndTime - EventStartTime;
							++OutCheckValues.EventCount;
							OutCheckValues.SumDepth += EventDepth;
							OutCheckValues.SumTimerIndex += Event.TimerIndex;
							return Trace::EEventEnumerate::Continue;
						});

					Time += TimeIncrement;
				}
			});
	}

	Stopwatch.Stop();
	OutCheckValues.EnumerationDuration = Stopwatch.GetAccumulatedTime();
	UE_LOG(TimingProfilerTests, Log, TEXT("BENCHMARK RESULT: %f seconds"), OutCheckValues.EnumerationDuration);
	UE_LOG(TimingProfilerTests, Log, TEXT("SessionTime: %f seconds"), SessionTime);
	UE_LOG(TimingProfilerTests, Log, TEXT("TimelineIndex: %u"), TimelineIndex);
	UE_LOG(TimingProfilerTests, Log, TEXT("Check Values: %f %llu %u %u"), OutCheckValues.TotalEventDuration, OutCheckValues.EventCount, OutCheckValues.SumDepth, OutCheckValues.SumTimerIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
