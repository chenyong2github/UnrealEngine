// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/Tests/TimingProfilerTests.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(TimingProfilerTests);

////////////////////////////////////////////////////////////////////////////////////////////////////
#if !WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateEventsToFile, "Insights.EnumerateEventsToFile", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateEventsToFile::RunTest(const FString& Parameters)
{
	double SessionTime = 0.0;
	uint32 TimelineIndex = (uint32)-1;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		TimelineIndex = FTimingProfilerTests::GetTimelineIndex(TEXT("GameThread"));

		AddErrorIfFalse(TimelineIndex != (uint32)-1, TEXT("Failed to get track named GameThread"));

		const Trace::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		FString EnumeratedEventsStr;
		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[Session, &EnumeratedEventsStr, TimerReader](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEvents(-1.0, Session->GetDurationSeconds() + 1.0,
					[&EnumeratedEventsStr, TimerReader](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
					{
						const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);

						EnumeratedEventsStr.Appendf(TEXT("%s %f %f %d\n"), Timer->Name, EventStartTime, EventEndTime, EventDepth);
						return Trace::EEventEnumerate::Continue;
					});
			});

		FString FilePath = FPaths::ProjectSavedDir() + TEXT("/EnumerateEventsToFile.txt");
		FFileHelper::SaveStringToFile(EnumeratedEventsStr, *FilePath);
	}

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateScopesToFile, "Insights.EnumerateScopesToFile", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateScopesToFile::RunTest(const FString& Parameters)
{
	double SessionTime = 0.0;
	uint32 TimelineIndex = (uint32)-1;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		TimelineIndex = FTimingProfilerTests::GetTimelineIndex(TEXT("GameThread"));

		AddErrorIfFalse(TimelineIndex != (uint32)-1, TEXT("Failed to get track named GameThread"));

		const Trace::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		FString EnumeratedEventsStr;
		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[Session, &EnumeratedEventsStr, TimerReader](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEvents(-1.0, Session->GetDurationSeconds() + 1.0,
					[&EnumeratedEventsStr, TimerReader](bool bStart, double Time, const Trace::FTimingProfilerEvent& Event)
					{
						const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);

						EnumeratedEventsStr.Appendf(TEXT("%s %d %f\n"), Timer->Name, (int32)bStart, Time);
						return Trace::EEventEnumerate::Continue;
					});
			});

		FString FilePath = FPaths::ProjectSavedDir() + TEXT("/EnumerateScopesToFile.txt");
		FFileHelper::SaveStringToFile(EnumeratedEventsStr, *FilePath);
	}

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByEndTimeAsyncAllTracks, "Insights.EnumerateByEndTimeAsyncAllTracks", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByEndTimeAsyncAllTracks::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 24.0 * 3600; //A day - Should be big enought to contain any valid session in [0, 0 + interval]
	Params.NumEnumerations = 1;
	Params.SortOrder = Trace::EEventSortOrder::ByEndTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByEndTimeAsyncGameThreadTrack, "Insights.EnumerateByEndTimeAsyncGameThreadTrack", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByEndTimeAsyncGameThreadTrack::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 24.0 * 3600; //A day - Should be big enought to contain any valid session in [0, 0 + interval]
	Params.NumEnumerations = 1;
	Params.SortOrder = Trace::EEventSortOrder::ByEndTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, true);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByEndTimeAllTracks10sIntervals, "Insights.EnumerateByEndTimeAllTracks10sIntervals", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByEndTimeAllTracks10sIntervals::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 10.0;
	Params.NumEnumerations = 100;
	Params.SortOrder = Trace::EEventSortOrder::ByEndTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByEndTimeAllTracks5sIntervals, "Insights.EnumerateByEndTimeAllTracks5sIntervals", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByEndTimeAllTracks5sIntervals::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 5.0;
	Params.NumEnumerations = 200;
	Params.SortOrder = Trace::EEventSortOrder::ByEndTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////Enumerate Async Ordered by Start Time Tests/////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByStartTimeAsyncAllTracks, "Insights.EnumerateByStartTimeAsyncAllTracks", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByStartTimeAsyncAllTracks::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 24.0 * 3600; //A day - Should be big enought to contain any valid session in [0, 0 + interval]
	Params.NumEnumerations = 1;
	Params.SortOrder = Trace::EEventSortOrder::ByStartTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByStartTimeAsyncGameThreadTrack, "Insights.EnumerateByStartTimeAsyncGameThreadTrack", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByStartTimeAsyncGameThreadTrack::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 24.0 * 3600; //A day - Should be big enought to contain any valid session in [0, 0 + interval]
	Params.NumEnumerations = 1;
	Params.SortOrder = Trace::EEventSortOrder::ByStartTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, true);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByStartTimeAllTracks10sIntervals, "Insights.EnumerateByStartTimeAllTracks10sIntervals", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByStartTimeAllTracks10sIntervals::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 10.0;
	Params.NumEnumerations = 100;
	Params.SortOrder = Trace::EEventSortOrder::ByStartTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(EnumerateByStartTimeAllTracks5sIntervals, "Insights.EnumerateByStartTimeAllTracks5sIntervals", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool EnumerateByStartTimeAllTracks5sIntervals::RunTest(const FString& Parameters)
{
	FTimingProfilerTests::FEnumerateTestParams Params;
	Params.Interval = 5.0;
	Params.NumEnumerations = 200;
	Params.SortOrder = Trace::EEventSortOrder::ByStartTime;

	FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(*this, Params, false);

	return !HasAnyErrors();
}

#endif // #if !WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

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

		TimelineIndex = GetTimelineIndex(TEXT("GameThread"));

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
void FTimingProfilerTests::RunEnumerateAsyncBenchmark(const FEnumerateTestParams& InParams, FCheckValues& OutCheckValues)
{
	UE_LOG(TimingProfilerTests, Log, TEXT("RUNNING ASYNC ENUMERATE BENCHMARK..."));

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

		TimelineIndex = GetTimelineIndex(TEXT("GameThread"));

		TArray<FCheckValues> TaskCheckValues;

		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[&OutCheckValues, &InParams, TimeIncrement, &TaskCheckValues](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				double Time = 0.0;
				for (int32 Index = 0; Index < InParams.NumEnumerations; ++Index)
				{
					Trace::ITimeline<Trace::FTimingProfilerEvent>::EnumerateAsyncParams Params;
					Params.IntervalStart = Time;
					Params.IntervalEnd = Time + InParams.Interval;
					Params.Resolution = 0.0;
					Params.SortOrder = InParams.SortOrder;
					Params.SetupCallback = [&TaskCheckValues](uint32 NumTasks)
					{
						TaskCheckValues.AddDefaulted(NumTasks);
					};
					Params.Callback = [&TaskCheckValues](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event, uint32 TaskIndex)
					{
						TaskCheckValues[TaskIndex].TotalEventDuration += EventEndTime - EventStartTime;
						++TaskCheckValues[TaskIndex].EventCount;
						TaskCheckValues[TaskIndex].SumDepth += EventDepth;
						TaskCheckValues[TaskIndex].SumTimerIndex += Event.TimerIndex;
						return Trace::EEventEnumerate::Continue;
					};

					Timeline.EnumerateEventsDownSampledAsync(Params);

					Time += TimeIncrement;
				}
			});

		for (auto& CheckValues : TaskCheckValues)
		{
			OutCheckValues.TotalEventDuration += CheckValues.TotalEventDuration;
			OutCheckValues.EventCount += CheckValues.EventCount;
			OutCheckValues.SumDepth += CheckValues.SumDepth;
			OutCheckValues.SumTimerIndex += CheckValues.SumTimerIndex;
		}
	}

	Stopwatch.Stop();
	OutCheckValues.EnumerationDuration = Stopwatch.GetAccumulatedTime();
	UE_LOG(TimingProfilerTests, Log, TEXT("ASYNC ENUMERATE BENCHMARK RESULT: %f seconds"), OutCheckValues.EnumerationDuration);
	UE_LOG(TimingProfilerTests, Log, TEXT("SessionTime: %f seconds"), SessionTime);
	UE_LOG(TimingProfilerTests, Log, TEXT("TimelineIndex: %u"), TimelineIndex);
	UE_LOG(TimingProfilerTests, Log, TEXT("Check Values: %f %llu %u %u"), OutCheckValues.TotalEventDuration, OutCheckValues.EventCount, OutCheckValues.SumDepth, OutCheckValues.SumTimerIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerTests::RunEnumerateAllTracksBenchmark(const FEnumerateTestParams& InParams, FCheckValues& OutCheckValues)
{
	UE_LOG(TimingProfilerTests, Log, TEXT("RUNNING ENUMERATE ALL TRACKS BENCHMARK..."));

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
			[&TimelineIndex, &TimingProfilerProvider, &OutCheckValues, &InParams, TimeIncrement](const Trace::FThreadInfo& ThreadInfo)
			{
				TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, TimelineIndex);

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
			});
	}

	Stopwatch.Stop();
	OutCheckValues.EnumerationDuration = Stopwatch.GetAccumulatedTime();
	UE_LOG(TimingProfilerTests, Log, TEXT("ENUMERATE ALL TRACKS BENCHMARK RESULT: %f seconds"), OutCheckValues.EnumerationDuration);
	UE_LOG(TimingProfilerTests, Log, TEXT("SessionTime: %f seconds"), SessionTime);
	UE_LOG(TimingProfilerTests, Log, TEXT("TimelineIndex: %u"), TimelineIndex);
	UE_LOG(TimingProfilerTests, Log, TEXT("Check Values: %f %llu %u %u"), OutCheckValues.TotalEventDuration, OutCheckValues.EventCount, OutCheckValues.SumDepth, OutCheckValues.SumTimerIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerTests::RunEnumerateAsyncAllTracksBenchmark(const FEnumerateTestParams& InParams, FCheckValues& OutCheckValues)
{
	UE_LOG(TimingProfilerTests, Log, TEXT("RUNNING ASYNC ENUMERATE ALL TRACKS BENCHMARK..."));

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

		TArray<FCheckValues> TaskCheckValues;

		const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(*Session.Get());
		ThreadProvider.EnumerateThreads(
			[&TimelineIndex, &TimingProfilerProvider, &OutCheckValues, &InParams, TimeIncrement, &TaskCheckValues](const Trace::FThreadInfo& ThreadInfo)
			{
				TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, TimelineIndex);

				TimingProfilerProvider.ReadTimeline(TimelineIndex,
					[&OutCheckValues, &InParams, TimeIncrement, &TaskCheckValues](const Trace::ITimingProfilerProvider::Timeline& Timeline)
					{
						double Time = 0.0;
						for (int32 Index = 0; Index < InParams.NumEnumerations; ++Index)
						{
							Trace::ITimeline<Trace::FTimingProfilerEvent>::EnumerateAsyncParams Params;
							Params.IntervalStart = Time;
							Params.IntervalEnd = Time + InParams.Interval;
							Params.Resolution = 0.0;
							Params.SortOrder = InParams.SortOrder;
							Params.SetupCallback = [&TaskCheckValues](uint32 NumTasks)
							{
								TaskCheckValues.AddDefaulted(NumTasks);
							};
							Params.Callback = [&OutCheckValues, &TaskCheckValues](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event, uint32 TaskIndex)
							{
								TaskCheckValues[TaskIndex].TotalEventDuration += EventEndTime - EventStartTime;
								++TaskCheckValues[TaskIndex].EventCount;
								TaskCheckValues[TaskIndex].SumDepth += EventDepth;
								TaskCheckValues[TaskIndex].SumTimerIndex += Event.TimerIndex;
								return Trace::EEventEnumerate::Continue;
							};

							Timeline.EnumerateEventsDownSampledAsync(Params);

							for (auto& CheckValues : TaskCheckValues)
							{
								OutCheckValues.TotalEventDuration += CheckValues.TotalEventDuration;
								OutCheckValues.EventCount += CheckValues.EventCount;
								OutCheckValues.SumDepth += CheckValues.SumDepth;
								OutCheckValues.SumTimerIndex += CheckValues.SumTimerIndex;
							}

							TaskCheckValues.Empty();
							Time += TimeIncrement;
						}
					});
			});
	}

	Stopwatch.Stop();
	OutCheckValues.EnumerationDuration = Stopwatch.GetAccumulatedTime();
	UE_LOG(TimingProfilerTests, Log, TEXT("ASYNC ENUMERATE ALL TRACKS BENCHMARK RESULT: %f seconds"), OutCheckValues.EnumerationDuration);
	UE_LOG(TimingProfilerTests, Log, TEXT("SessionTime: %f seconds"), SessionTime);
	UE_LOG(TimingProfilerTests, Log, TEXT("TimelineIndex: %u"), TimelineIndex);
	UE_LOG(TimingProfilerTests, Log, TEXT("Check Values: %f %llu %u %u"), OutCheckValues.TotalEventDuration, OutCheckValues.EventCount, OutCheckValues.SumDepth, OutCheckValues.SumTimerIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FTimingProfilerTests::GetTimelineIndex(const TCHAR* InName)
{
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());
	const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(*Session.Get());
	uint32 TimelineIndex = (uint32) -1;
	ThreadProvider.EnumerateThreads(
		[&TimelineIndex, &TimingProfilerProvider, InName](const Trace::FThreadInfo& ThreadInfo)
		{
			if (!FCString::Strcmp(ThreadInfo.Name, InName))
			{
				TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, TimelineIndex);
			}
		});

	return TimelineIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingProfilerTests::RunEnumerateSyncAsyncComparisonTest(FAutomationTestBase& Test, const FEnumerateTestParams& InParams, bool bGameThreadOnly)
{
	FTimingProfilerTests::FCheckValues CheckValues;
	FTimingProfilerTests::FCheckValues CheckValuesAsync;

	if (bGameThreadOnly)
	{
		FTimingProfilerTests::RunEnumerateBenchmark(InParams, CheckValues);
		FTimingProfilerTests::RunEnumerateAsyncBenchmark(InParams, CheckValuesAsync);
	}
	else
	{
		FTimingProfilerTests::RunEnumerateAllTracksBenchmark(InParams, CheckValues);
		FTimingProfilerTests::RunEnumerateAsyncAllTracksBenchmark(InParams, CheckValuesAsync);
	}

	FTimingProfilerTests::VerifyCheckValues(Test, CheckValues, CheckValuesAsync);

	Test.AddInfo(FString::Printf(TEXT("Enumeration Duration: %f seconds."), CheckValues.EnumerationDuration));
	Test.AddInfo(FString::Printf(TEXT("Async Enumeration Duration: %f seconds."), CheckValuesAsync.EnumerationDuration));

	return !Test.HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerTests::VerifyCheckValues(FAutomationTestBase& Test, FCheckValues First, FCheckValues Second)
{
	Test.TestEqual(TEXT("SessionDuration"), First.SessionDuration, Second.SessionDuration, 1.e-6);
	Test.TestEqual(TEXT("TotalEventDuration"), First.TotalEventDuration, Second.TotalEventDuration, 1.e-3);
	Test.TestEqual(TEXT("EventCount"), First.EventCount, Second.EventCount);
	Test.TestEqual(TEXT("SumDepth"), (uint64) First.SumDepth, (uint64) Second.SumDepth);
	Test.TestEqual(TEXT("SumTimerIndex"), (uint64) First.SumTimerIndex, (uint64) Second.SumTimerIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

