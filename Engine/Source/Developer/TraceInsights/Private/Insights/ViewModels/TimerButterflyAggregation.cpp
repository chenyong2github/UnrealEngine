// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimerButterflyAggregation.h"

#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimerButterflyAggregationWorker
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerButterflyAggregationWorker : public IStatsAggregationWorker
{
public:
	FTimerButterflyAggregationWorker(TSharedPtr<const Trace::IAnalysisSession> InSession, double InStartTime, double InEndTime, const TSet<uint32>& InCpuThreads, bool bInIncludeGpuThread)
		: Session(InSession)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, CpuThreads(InCpuThreads)
		, bIncludeGpuThread(bInIncludeGpuThread)
		, ResultButterfly()
	{
	}

	virtual ~FTimerButterflyAggregationWorker() {}

	virtual void DoWork() override;

	Trace::ITimingProfilerButterfly* GetResultButterfly() const { return ResultButterfly.Get(); }
	void ResetResults() { ResultButterfly.Reset(); }

private:
	TSharedPtr<const Trace::IAnalysisSession> Session;
	double StartTime;
	double EndTime;
	TSet<uint32> CpuThreads;
	bool bIncludeGpuThread;
	TUniquePtr<Trace::ITimingProfilerButterfly> ResultButterfly;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerButterflyAggregationWorker::DoWork()
{
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		// Suspend analysis in order to avoid write locks (ones blocked by the read lock below) to further block other read locks.
		//Trace::FAnalysisSessionSuspensionScope SessionPauseScope(*Session.Get());

		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		auto ThreadFilter = [this](uint32 ThreadId)
		{
			return CpuThreads.Contains(ThreadId);
		};

		ResultButterfly.Reset(TimingProfilerProvider.CreateButterfly(StartTime, EndTime, ThreadFilter, bIncludeGpuThread));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimerButterflyAggregator
////////////////////////////////////////////////////////////////////////////////////////////////////

IStatsAggregationWorker* FTimerButterflyAggregator::CreateWorker(TSharedPtr<const Trace::IAnalysisSession> InSession)
{
	bool bIsGpuTrackVisible = false;
	TSet<uint32> CpuThreads;

	TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (Wnd.IsValid())
	{
		TSharedPtr<STimingView> TimingView = Wnd->GetTimingView();
		if (TimingView.IsValid())
		{
			bIsGpuTrackVisible = TimingView->IsGpuTrackVisible();

			TSharedPtr<FThreadTimingSharedState> ThreadTimingSharedState = TimingView->GetThreadTimingSharedState();
			if (ThreadTimingSharedState.IsValid())
			{
				ThreadTimingSharedState->GetVisibleCpuThreads(CpuThreads);
			}
		}
	}

	return new FTimerButterflyAggregationWorker(InSession, GetIntervalStartTime(), GetIntervalEndTime(), CpuThreads, bIsGpuTrackVisible);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Trace::ITimingProfilerButterfly* FTimerButterflyAggregator::GetResultButterfly() const
{
	// It can only be called from OnFinishedCallback.
	check(IsFinished());

	FTimerButterflyAggregationWorker* Worker = (FTimerButterflyAggregationWorker*)GetWorker();
	check(Worker != nullptr);

	return Worker->GetResultButterfly();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerButterflyAggregator::ResetResults()
{
	// It can only be called from OnFinishedCallback.
	check(IsFinished());

	FTimerButterflyAggregationWorker* Worker = (FTimerButterflyAggregationWorker*)GetWorker();
	check(Worker != nullptr);

	Worker->ResetResults();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
