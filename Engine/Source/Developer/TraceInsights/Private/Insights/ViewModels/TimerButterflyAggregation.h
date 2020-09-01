// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/ViewModels/StatsAggregator.h"
#include "TraceServices/Model/TimingProfiler.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerButterflyAggregator : public FStatsAggregator
{
public:
	FTimerButterflyAggregator() : FStatsAggregator(TEXT("Butterfly")) {}
	virtual ~FTimerButterflyAggregator() {}

	Trace::ITimingProfilerButterfly* GetResultButterfly() const;
	void ResetResults();

protected:
	virtual IStatsAggregationWorker* CreateWorker(TSharedPtr<const Trace::IAnalysisSession> InSession) override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
