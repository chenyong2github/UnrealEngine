// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimerAggregation.h"

#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimerAggregationWorker
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerAggregationWorker : public IStatsAggregationWorker
{
public:
	FTimerAggregationWorker(TSharedPtr<const Trace::IAnalysisSession> InSession, double InStartTime, double InEndTime, const TSet<uint32>& InCpuThreads, bool bInIncludeGpuThread)
		: Session(InSession)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, CpuThreads(InCpuThreads)
		, bIncludeGpuThread(bInIncludeGpuThread)
		, ResultTable()
	{
	}

	virtual ~FTimerAggregationWorker() {}

	virtual void DoWork() override;

	Trace::ITable<Trace::FTimingProfilerAggregatedStats>* GetResultTable() const { return ResultTable.Get(); }
	void ResetResults() { ResultTable.Reset(); }

private:
	TSharedPtr<const Trace::IAnalysisSession> Session;
	double StartTime;
	double EndTime;
	TSet<uint32> CpuThreads;
	bool bIncludeGpuThread;
	TUniquePtr<Trace::ITable<Trace::FTimingProfilerAggregatedStats>> ResultTable;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerAggregationWorker::DoWork()
{
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		// Suspend analysis in order to avoid write locks (ones blocked by the read lock below) to further block other read locks.
		//Trace::FAnalysisSessionSuspensionScope SessionPauseScope(*Session.Get());

		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		auto CpuThreadFilter = [this](uint32 ThreadId)
		{
			return CpuThreads.Contains(ThreadId);
		};

		ResultTable.Reset(TimingProfilerProvider.CreateAggregation(StartTime, EndTime, CpuThreadFilter, bIncludeGpuThread));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimerAggregator
////////////////////////////////////////////////////////////////////////////////////////////////////

IStatsAggregationWorker* FTimerAggregator::CreateWorker(TSharedPtr<const Trace::IAnalysisSession> InSession)
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

	return new FTimerAggregationWorker(InSession, GetIntervalStartTime(), GetIntervalEndTime(), CpuThreads, bIsGpuTrackVisible);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Trace::ITable<Trace::FTimingProfilerAggregatedStats>* FTimerAggregator::GetResultTable() const
{
	// It can only be called from OnFinishedCallback.
	check(IsFinished());

	FTimerAggregationWorker* Worker = (FTimerAggregationWorker*)GetWorker();
	check(Worker != nullptr);

	return Worker->GetResultTable();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerAggregator::ResetResults()
{
	// It can only be called from OnFinishedCallback.
	check(IsFinished());

	FTimerAggregationWorker* Worker = (FTimerAggregationWorker*)GetWorker();
	check(Worker != nullptr);

	Worker->ResetResults();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
