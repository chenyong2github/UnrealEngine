// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicResolutionProxy.h"
#include "DynamicResolutionState.h"

#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "RenderCore.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "DynamicRHI.h"
#include "UnrealEngine.h"
#include "RenderGraphEvent.h"


static TAutoConsoleVariable<float> CVarDynamicResMinSP(
	TEXT("r.DynamicRes.MinScreenPercentage"),
	DynamicRenderScaling::FractionToPercentage(DynamicRenderScaling::FHeuristicSettings::kDefaultMinResolutionFraction),
	TEXT("Minimal primary screen percentage."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<float> CVarDynamicResMaxSP(
	TEXT("r.DynamicRes.MaxScreenPercentage"),
	DynamicRenderScaling::FractionToPercentage(DynamicRenderScaling::FHeuristicSettings::kDefaultMaxResolutionFraction),
	TEXT("Maximal primary screen percentage."),
	ECVF_Default);

// TODO: Seriously need a centralized engine perf manager.
static TAutoConsoleVariable<float> CVarFrameTimeBudget(
	TEXT("r.DynamicRes.FrameTimeBudget"),
	33.3f,
	TEXT("Frame's time budget in milliseconds."),
	ECVF_RenderThreadSafe | ECVF_Default);



static TAutoConsoleVariable<float> CVarTargetedGPUHeadRoomPercentage(
	TEXT("r.DynamicRes.TargetedGPUHeadRoomPercentage"),
	10.0f,
	TEXT("Targeted GPU headroom (in percent from r.DynamicRes.FrameTimeBudget)."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<int32> CVarHistorySize(
	TEXT("r.DynamicRes.HistorySize"),
	16,
	TEXT("Number of frames keept in the history."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<float> CVarFrameWeightExponent(
	TEXT("r.DynamicRes.FrameWeightExponent"),
	0.9f,
	TEXT("Recursive weight of frame N-1 against frame N."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<int32> CVarFrameChangePeriod(
	TEXT("r.DynamicRes.MinResolutionChangePeriod"),
	8,
	TEXT("Minimal number of frames between resolution changes, important to avoid input ")
	TEXT("sample position interferences in TAA upsample."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<float> CVarIncreaseAmortizationFactor(
	TEXT("r.DynamicRes.IncreaseAmortizationBlendFactor"),
	DynamicRenderScaling::FHeuristicSettings::kDefaultIncreaseAmortizationFactor,
	TEXT("Amortization blend factor when scale resolution back up to reduce resolution fraction oscillations."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<float> CVarChangeThreshold(
	TEXT("r.DynamicRes.ChangePercentageThreshold"),
	DynamicRenderScaling::FractionToPercentage(DynamicRenderScaling::FHeuristicSettings::kDefaultChangeThreshold),
	TEXT("Minimal increase percentage threshold to alow when changing resolution."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<int32> CVarMaxConsecutiveOverbudgetGPUFrameCount(
	TEXT("r.DynamicRes.MaxConsecutiveOverbudgetGPUFrameCount"),
	2,
	TEXT("Maximum number of consecutive frame tolerated over GPU budget."),
	ECVF_RenderThreadSafe | ECVF_Default);

static TAutoConsoleVariable<int32> CVarTimingMeasureModel(
	TEXT("r.DynamicRes.GPUTimingMeasureMethod"),
	0,
	TEXT("Selects the method to use to measure GPU timings.\n")
	TEXT(" 0: Same as stat unit (default);\n 1: Timestamp queries."),
	ECVF_RenderThreadSafe | ECVF_Default);


DynamicRenderScaling::FHeuristicSettings GetPrimaryDynamicResolutionSettings()
{
	DynamicRenderScaling::FHeuristicSettings BudgetSetting;
	BudgetSetting.Model = DynamicRenderScaling::EHeuristicModel::Quadratic;
	BudgetSetting.MinResolutionFraction      = DynamicRenderScaling::GetPercentageCVarToFraction(CVarDynamicResMinSP);
	BudgetSetting.MaxResolutionFraction      = DynamicRenderScaling::GetPercentageCVarToFraction(CVarDynamicResMaxSP);
	// BudgetSetting.BudgetMs depends on the cost of other buckets.
	BudgetSetting.ChangeThreshold            = DynamicRenderScaling::GetPercentageCVarToFraction(CVarChangeThreshold);
	BudgetSetting.IncreaseAmortizationFactor = CVarIncreaseAmortizationFactor.GetValueOnRenderThread();

	// CVarTargetedGPUHeadRoomPercentage is taken into account on the entire frame instead.
	BudgetSetting.TargetedHeadRoom = 0.0f;
	return BudgetSetting;
}

DynamicRenderScaling::FBudget GDynamicPrimaryResolutionFraction(TEXT("DynamicPrimaryResolution"), &GetPrimaryDynamicResolutionSettings);


static float TimeStampQueryResultToMiliSeconds(uint64 TimestampResult)
{
	return float(TimestampResult) / 1000.0f;
}


FDynamicResolutionHeuristicProxy::FDynamicResolutionHeuristicProxy()
{
	check(IsInGameThread());

	FrameCounter = 0;

	ResetInternal();
}

FDynamicResolutionHeuristicProxy::~FDynamicResolutionHeuristicProxy()
{
	check(IsInRenderingThread());
}

void FDynamicResolutionHeuristicProxy::Reset_RenderThread()
{
	check(IsInRenderingThread());

	ResetInternal();
}

void FDynamicResolutionHeuristicProxy::ResetInternal()
{
	PreviousFrameIndex = -1;
	HistorySize = 0;
	BudgetHistorySizes.SetAll(0);
	History.Reset();

	NumberOfFramesSinceScreenPercentageChange = 0;
	CurrentFrameResolutionFractions.SetAll(1.0f);

	// Ignore previous frame timings.
	IgnoreFrameRemainingCount = 1;
}

uint64 FDynamicResolutionHeuristicProxy::CreateNewPreviousFrameTimings_RenderThread(
	float GameThreadTimeMs, float RenderThreadTimeMs)
{
	check(IsInRenderingThread());

	// Early return if want to ignore frames.
	if (IgnoreFrameRemainingCount > 0)
	{
		IgnoreFrameRemainingCount--;
		return FDynamicResolutionHeuristicProxy::kInvalidEntryId;
	}

	ResizeHistoryIfNeeded();

	// Update history state.
	{
		int32 NewHistoryEntryIndex = (PreviousFrameIndex + 1) % History.Num();
		History[NewHistoryEntryIndex] = FrameHistoryEntry();
		History[NewHistoryEntryIndex].ResolutionFractions = CurrentFrameResolutionFractions;
		History[NewHistoryEntryIndex].GameThreadTimeMs = GameThreadTimeMs;
		History[NewHistoryEntryIndex].RenderThreadTimeMs = RenderThreadTimeMs;
		PreviousFrameIndex = NewHistoryEntryIndex;
		HistorySize = FMath::Min(HistorySize + 1, History.Num());

		for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
		{
			const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
			BudgetHistorySizes[Budget] = FMath::Min(BudgetHistorySizes[Budget] + 1, History.Num());
		}
	}

	return ++FrameCounter;
}

void FDynamicResolutionHeuristicProxy::CommitPreviousFrameGPUTimings_RenderThread(
	uint64 HistoryFrameId,
	float TotalFrameGPUBusyTimeMs,
	float DynamicResolutionGPUBusyTimeMs,
	const DynamicRenderScaling::TMap<float>& BudgetTimingMs)
{
	check(TotalFrameGPUBusyTimeMs >= 0.0f);
	check(DynamicResolutionGPUBusyTimeMs >= 0.0f);

	// Conveniently early return if invalid history frame id.
	if (HistoryFrameId == FDynamicResolutionHeuristicProxy::kInvalidEntryId ||
		HistoryFrameId <= (FrameCounter - HistorySize))
	{
		return;
	}

	int32 EntryId = (History.Num() + PreviousFrameIndex - int32(FrameCounter - HistoryFrameId)) % History.Num();

	// Make sure not overwriting.
	check(History[EntryId].TotalFrameGPUBusyTimeMs == -1.0f);

	History[EntryId].TotalFrameGPUBusyTimeMs = TotalFrameGPUBusyTimeMs;
	History[EntryId].GlobalDynamicResolutionTimeMs = DynamicResolutionGPUBusyTimeMs;
	History[EntryId].BudgetTimingMs = BudgetTimingMs;
}

void FDynamicResolutionHeuristicProxy::RefreshCurentFrameResolutionFraction_RenderThread()
{
	// Compute new frame resolution fractions only if have an history to work with.
	if (HistorySize == 0)
	{
		return;
	}

	// GPU time budget per frame=.
	const float FrameTimeBudgetMs = CVarFrameTimeBudget.GetValueOnRenderThread();

	// Targeted GPU time, lower than budget to diggest noise.
	const float TargetedGPUBusyTimeMs = FrameTimeBudgetMs * (1.0f - CVarTargetedGPUHeadRoomPercentage.GetValueOnRenderThread() / 100.0f);

	const float FrameWeightExponent = CVarFrameWeightExponent.GetValueOnRenderThread();
	const int32 MaxConsecutiveOverbudgetGPUFrameCount = FMath::Max(CVarMaxConsecutiveOverbudgetGPUFrameCount.GetValueOnRenderThread(), 2);
	
	// Find the first frame that have data.
	int32 StartFrameId = 0;
	{
		for (; StartFrameId < HistorySize; StartFrameId++)
		{
			const FrameHistoryEntry& FrameEntry = GetPreviousFrameEntry(StartFrameId);

			// Ignores frames that does not have any GPU timing yet. 
			if (FrameEntry.TotalFrameGPUBusyTimeMs < 0)
			{
				continue;
			}
			else
			{
				break;
			}
		}
	}

	// Not enough data to work with.
	if (HistorySize - StartFrameId <= MaxConsecutiveOverbudgetGPUFrameCount)
	{
		return;
	}

	check(StartFrameId + MaxConsecutiveOverbudgetGPUFrameCount < HistorySize);

	// Look whether all the last MaxConsecutiveOverbudgetGPUFrameCount frames are over budget.
	bool bGPUFrameOverbudgetPanic = true;
	for (int32 BrowsingFrameId = StartFrameId; BrowsingFrameId < StartFrameId + MaxConsecutiveOverbudgetGPUFrameCount; BrowsingFrameId++)
	{
		const FrameHistoryEntry& FrameEntry = GetPreviousFrameEntry(BrowsingFrameId);
		bGPUFrameOverbudgetPanic &= (FrameEntry.TotalFrameGPUBusyTimeMs > FrameTimeBudgetMs);
	}

	// Whether a cruising resolution change can happen.
	bool bCanChangeResolution = NumberOfFramesSinceScreenPercentageChange >= CVarFrameChangePeriod.GetValueOnRenderThread();

	// Array to know how much a frame is overbudget
	//TArray<float, TInlineAllocator<256>> TotalBudgetOverBudgetSavings;
	//TotalBudgetOverBudgetSavings.SetNumZeroed(MaxFrameId);

	// Function to detect whether the history size is big enough for detecting an overbudget
	auto HasNotEnoughDataForOverBudgetDetection = [&](int32 InHistorySize)
	{
		return (InHistorySize - StartFrameId) <= MaxConsecutiveOverbudgetGPUFrameCount;
	};

	// Function to know whether a bucket is overbudget.
	auto IsBudgetOverBudget = [&](const DynamicRenderScaling::FBudget& Budget, const DynamicRenderScaling::FHeuristicSettings& BudgetSetting)
	{
		check(BudgetSetting.IsEnabled());

		if (HasNotEnoughDataForOverBudgetDetection(BudgetHistorySizes[Budget]))
		{
			// Not enough data to work with.
			return false;
		}

		bool bBudgetIsOverBudget = true;

		for (int32 BrowsingFrameId = StartFrameId; BrowsingFrameId < StartFrameId + MaxConsecutiveOverbudgetGPUFrameCount; BrowsingFrameId++)
		{
			const FrameHistoryEntry& FrameEntry = GetPreviousFrameEntry(BrowsingFrameId);

			float bOverBudget = FrameEntry.BudgetTimingMs[Budget] - BudgetSetting.BudgetMs;
			bBudgetIsOverBudget &= bOverBudget > 0.0f;
		}

		return bBudgetIsOverBudget;
	};

	// Compute
	auto ComputeMaxFrameId = [&](const DynamicRenderScaling::FBudget& Budget, bool bBudgetIsOverBudget)
	{
		int32 MaxFrameId = BudgetHistorySizes[Budget];
		
		if (bGPUFrameOverbudgetPanic || bBudgetIsOverBudget)
		{
			MaxFrameId = FMath::Min(MaxFrameId, StartFrameId + MaxConsecutiveOverbudgetGPUFrameCount);
		}

		check(MaxFrameId <= BudgetHistorySizes[Budget]);
		check(MaxFrameId <= HistorySize);
		return MaxFrameId;
	};

	DynamicRenderScaling::TMap<float> NewResolutionFractions;
	bool bCommitResolutionChange = false;

	// Execute heuristic on the bucket.
	auto ComputeNewResolutionFraction = [&](const DynamicRenderScaling::FBudget& Budget, const DynamicRenderScaling::FHeuristicSettings& BudgetSetting, bool bBudgetIsOverBudget)
	{
		check(BudgetSetting.IsEnabled());

		float NewResolutionFraction = CurrentFrameResolutionFractions[Budget];
		if (!HasNotEnoughDataForOverBudgetDetection(BudgetHistorySizes[Budget]))
		{
			float CurrentResolutionFraction = CurrentFrameResolutionFractions[Budget];
			if (BudgetSetting.bModelScalesWithPrimaryScreenPercentage)
			{
				CurrentResolutionFraction *= CurrentFrameResolutionFractions[GDynamicPrimaryResolutionFraction];
			}

			int32 MaxFrameId = ComputeMaxFrameId(Budget, bBudgetIsOverBudget);

			// Find out a new resolution fraction based on whether this bucket is overbudget.
			float MostRecentResolutionFractionScale = 1.0f;
			float SuggestedResolutionFraction = 0.0f;
			float SuggestedResolutionFractionWeight = 0.0f;
			float Weight = 1.0f;
			for (int32 BrowsingFrameId = StartFrameId; BrowsingFrameId < MaxFrameId; BrowsingFrameId++)
			{
				const FrameHistoryEntry& FrameEntry = GetPreviousFrameEntry(BrowsingFrameId);
				float TimingMs = FrameEntry.BudgetTimingMs[Budget];

				float ResolutionFraction = FrameEntry.ResolutionFractions[Budget];
				if (BudgetSetting.bModelScalesWithPrimaryScreenPercentage)
				{
					ResolutionFraction *= FrameEntry.ResolutionFractions[GDynamicPrimaryResolutionFraction];
				}

				// Removing the savings from other bucket
				//if (Budget != EDynamicResolutionTimingBudget::PrimaryScreenPercentage)
				//{
				//	TimingMs -= TotalBudgetOverBudgetSavings[BrowsingFrameId];
				//}

				// Estimate how much a resolution fraction should be changed.
				float ResolutionFractionFactor = BudgetSetting.EstimateResolutionFactor(TimingMs);

				// Don't increase the resolution if frame overbudget.
				if (bGPUFrameOverbudgetPanic && ResolutionFractionFactor > 1.0f)
				{
					ResolutionFractionFactor = 1.0f;
				}

				float AdjustedResolutionFraction = ResolutionFractionFactor * ResolutionFraction;

				// Track how much saving can be done in a bucket to max out PrimaryScreenPercentage
				//if (Budget != EDynamicResolutionTimingBudget::PrimaryScreenPercentage)
				//{
				//	float EstimatedSavings = (
				//		FrameEntry.BudgetTimingMs[Budget] *
				//		(1.0f - BudgetSetting.EstimateTimeFactor(FrameEntry.ResolutionFractions[Budget], AdjustedResolutionFraction)));
				//
				//	TotalBudgetOverBudgetSavings[BrowsingFrameId] += EstimatedSavings;
				//}

				SuggestedResolutionFraction += AdjustedResolutionFraction * Weight;
				SuggestedResolutionFractionWeight += Weight;
				Weight *= FrameWeightExponent;

				if (BrowsingFrameId == StartFrameId && BudgetSetting.bModelScalesWithPrimaryScreenPercentage)
				{
					MostRecentResolutionFractionScale = FrameEntry.ResolutionFractions[GDynamicPrimaryResolutionFraction];
				}
			}

			NewResolutionFraction = BudgetSetting.CorrectNewResolutionFraction(
				CurrentResolutionFraction,
				SuggestedResolutionFraction / SuggestedResolutionFractionWeight,
				MostRecentResolutionFractionScale);

			NewResolutionFraction /= MostRecentResolutionFractionScale;

			bool bTakeNewResolutionFraction = (
				BudgetSetting.DoesResolutionChangeEnough(CurrentResolutionFraction, NewResolutionFraction, bCanChangeResolution) ||
				bBudgetIsOverBudget);

			if (bTakeNewResolutionFraction)
			{
				bCommitResolutionChange = true;
			}
			else
			{
				NewResolutionFraction = CurrentFrameResolutionFractions[Budget];
			}
		}

		return NewResolutionFraction;
	};

	// Estimate the new cost of the bucket.
	auto EstimateBudgetCost = [&](const DynamicRenderScaling::FBudget& Budget, bool bBudgetIsOverBudget)
	{
		const DynamicRenderScaling::FHeuristicSettings& BudgetSetting = Budget.GetSettings();
		check(BudgetSetting.IsEnabled());

		int32 MaxFrameId = ComputeMaxFrameId(Budget, bBudgetIsOverBudget);

		float CurrentResolutionFraction = CurrentFrameResolutionFractions[Budget];
		float NewResolutionFraction = NewResolutionFractions[Budget];

		ensure(MaxFrameId != StartFrameId);

		float BudgetPredictedCost = 0.0;
		float BudgetPredictedCostWeight = 0.0;
		float Weight = 1.0f;
		for (int32 BrowsingFrameId = StartFrameId; BrowsingFrameId < MaxFrameId; BrowsingFrameId++)
		{
			const FrameHistoryEntry& FrameEntry = GetPreviousFrameEntry(BrowsingFrameId);
			float EstimatedCost = (
				FrameEntry.BudgetTimingMs[Budget] *
				BudgetSetting.EstimateTimeFactor(CurrentResolutionFraction, NewResolutionFraction));

			BudgetPredictedCost += EstimatedCost * Weight;
			BudgetPredictedCostWeight += Weight;
			Weight *= FrameWeightExponent;
		}

		return BudgetPredictedCost / BudgetPredictedCostWeight;
	};

	// Iterate through bucket and adjust them.
	float TotalStaticBudgetPredictedCost = 0.0f;
	float TotalPrimaryScreenPercentageDynamicBudgetPredictedCost = 0.0f;
	for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
	{
		const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
		const DynamicRenderScaling::FHeuristicSettings& HeuristicSettings = Budget.GetSettings();

		// Primary screen percentage is done last.
		if (Budget == GDynamicPrimaryResolutionFraction)
		{
			continue;
		}

		// Skip if the bucket is not enabled.
		if (HeuristicSettings.IsEnabled())
		{
			bool bBudgetIsOverBudget = IsBudgetOverBudget(Budget, HeuristicSettings);

			NewResolutionFractions[Budget] = ComputeNewResolutionFraction(Budget, HeuristicSettings, bBudgetIsOverBudget);

			float BudgetCost = EstimateBudgetCost(Budget, bBudgetIsOverBudget);
			if (HeuristicSettings.bModelScalesWithPrimaryScreenPercentage)
			{
				TotalPrimaryScreenPercentageDynamicBudgetPredictedCost += BudgetCost;
			}
			else
			{
				TotalStaticBudgetPredictedCost += BudgetCost;
			}

			// Reset the size of the history for the bucket when it is over budget.
			if (bBudgetIsOverBudget)
			{
				BudgetHistorySizes[Budget] = 0;
			}
		}
		else
		{
			NewResolutionFractions[Budget] = 1.0f;
			continue;
		}
	}

	// Takes care of primary screen percentage to finally fit the frame within budget
	{
		const DynamicRenderScaling::FBudget& Budget = GDynamicPrimaryResolutionFraction;

		// Set the budget of the budget.
		DynamicRenderScaling::FHeuristicSettings HeuristicSettings = Budget.GetSettings();
		HeuristicSettings.BudgetMs = FMath::Max(TargetedGPUBusyTimeMs - TotalStaticBudgetPredictedCost, 0.5f);
		check(HeuristicSettings.IsEnabled());

		bool bBudgetIsOverBudget = IsBudgetOverBudget(Budget, HeuristicSettings);

		NewResolutionFractions[Budget] = ComputeNewResolutionFraction(Budget, HeuristicSettings, bBudgetIsOverBudget);
	}

	// Commit the new resolution fractions.
	if (bCommitResolutionChange)
	{
		NumberOfFramesSinceScreenPercentageChange = 0;
		CurrentFrameResolutionFractions = NewResolutionFractions;
	}
	else
	{
		NumberOfFramesSinceScreenPercentageChange++;
	}
}

// static
DynamicRenderScaling::TMap<float> FDynamicResolutionHeuristicProxy::GetResolutionFractionUpperBounds()
{
	DynamicRenderScaling::TMap<float> MaxResolutionFractions;
	for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
	{
		const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
		MaxResolutionFractions[Budget] = Budget.GetSettings().MaxResolutionFraction;
	}
	return MaxResolutionFractions;
}


/** Returns the view fraction that should be used for current frame. */
DynamicRenderScaling::TMap<float> FDynamicResolutionHeuristicProxy::QueryCurentFrameResolutionFractions_Internal() const
{
	DynamicRenderScaling::TMap<float> MaxResolutionFractions = GetResolutionFractionUpperBounds();
	DynamicRenderScaling::TMap<float> ResolutionFractions = CurrentFrameResolutionFractions;
	for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
	{
		const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
		ResolutionFractions[Budget] = FMath::Min(ResolutionFractions[Budget], MaxResolutionFractions[Budget]);
	}
	return ResolutionFractions;
}

void FDynamicResolutionHeuristicProxy::ResizeHistoryIfNeeded()
{
	uint32 DesiredHistorySize = FMath::Max(1, CVarHistorySize.GetValueOnRenderThread());

	if (History.Num() == DesiredHistorySize)
	{
		return;
	}

	TArray<FrameHistoryEntry> NewHistory;
	NewHistory.SetNum(DesiredHistorySize);

	int32 NewHistorySize = FMath::Min(HistorySize, NewHistory.Num());
	int32 NewPreviousFrameIndex = NewHistorySize - 1;

	for (int32 i = 0; i < NewHistorySize; i++)
	{
		NewHistory[NewPreviousFrameIndex - i] = History[(PreviousFrameIndex - i) % History.Num()];
	}

	History = NewHistory;
	HistorySize = NewHistorySize;
	PreviousFrameIndex = NewPreviousFrameIndex;
}


/**
 * Render thread proxy for engine's dynamic resolution state.
 */
class FDefaultDynamicResolutionStateProxy
{
public:
	FDefaultDynamicResolutionStateProxy()
	{
		check(IsInGameThread());
		InFlightFrames.SetNum(4);
		CurrentFrameInFlightIndex = -1;
		bUseTimeQueriesThisFrame = false;
	}

	~FDefaultDynamicResolutionStateProxy()
	{
		check(IsInRenderingThread());
		checkf(InFlightFrames.Num()==0, TEXT("Ensure the object is properly deinitialized by Finalize call"));
	}

	void Reset()
	{
		check(IsInRenderingThread());

		// Reset heuristic.
		Heuristic.Reset_RenderThread();

		// Set invalid heuristic's entry id on all inflight frames.
		for (auto& InFlightFrame : InFlightFrames)
		{
			InFlightFrame.HeuristicHistoryEntry = FDynamicResolutionHeuristicProxy::kInvalidEntryId;
		}
	}

	void BeginFrame(FRHICommandList& RHICmdList, float PrevGameThreadTimeMs)
	{
		check(IsInRenderingThread());

		if (DynamicRenderScaling::IsSupported())
		{
			DynamicRenderScaling::UpdateHeuristicsSettings();

			DynamicRenderScaling::TMap<bool> bIsBudgetEnabled;
			bIsBudgetEnabled.SetAll(false);

			for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
			{
				const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
				bIsBudgetEnabled[Budget] = Budget.GetSettings().IsEnabled();
			}

			DynamicRenderScaling::BeginFrame(bIsBudgetEnabled);
		}

		// Query render thread time Ms.
		float PrevRenderThreadTimeMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);

		bUseTimeQueriesThisFrame = GSupportsTimestampRenderQueries && CVarTimingMeasureModel.GetValueOnRenderThread() == 1;

		if (bUseTimeQueriesThisFrame)
		{
			// Create the query pool
			if (!QueryPool.IsValid())
				QueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime);

			// Process to the in-flight frames that had their queries fully landed.
			HandLandedQueriesToHeuristic(/* bWait = */ false);

			// Finds a new CurrentFrameInFlightIndex that does not have any pending queries.
			FindNewInFlightIndex();

			InFlightFrameQueries& InFlightFrame = InFlightFrames[CurrentFrameInFlightIndex];

			// Feed the thread timings to the heuristic.
			InFlightFrame.HeuristicHistoryEntry = Heuristic.CreateNewPreviousFrameTimings_RenderThread(
				PrevGameThreadTimeMs, PrevRenderThreadTimeMs);

			check(QueryPool.IsValid());
			InFlightFrame.BeginFrameQuery = QueryPool->AllocateQuery();
			RHICmdList.EndRenderQuery(InFlightFrame.BeginFrameQuery.GetQuery());
		}
		else
		{
			// If RHI does not support GPU busy time queries, fall back to what stat unit does.
			ensure(GRHISupportsFrameCyclesBubblesRemoval);
			float PrevFrameGPUTimeMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());

			uint64 HistoryEntryId = Heuristic.CreateNewPreviousFrameTimings_RenderThread(
				PrevGameThreadTimeMs, PrevRenderThreadTimeMs);

			const DynamicRenderScaling::TMap<uint64>& LattestTimings = DynamicRenderScaling::GetLastestTimings();
			DynamicRenderScaling::TMap<float> BudgetTimingMs;
			for (TLinkedList<DynamicRenderScaling::FBudget*>::TIterator BudgetIt(DynamicRenderScaling::FBudget::GetGlobalList()); BudgetIt; BudgetIt.Next())
			{
				const DynamicRenderScaling::FBudget& Budget = **BudgetIt;
				BudgetTimingMs[Budget] = TimeStampQueryResultToMiliSeconds(LattestTimings[Budget]);
			}
			BudgetTimingMs[GDynamicPrimaryResolutionFraction] = PrevFrameGPUTimeMs;


			Heuristic.CommitPreviousFrameGPUTimings_RenderThread(HistoryEntryId,
				/* TotalFrameGPUBusyTimeMs = */ PrevFrameGPUTimeMs,
				/* DynamicResolutionGPUBusyTimeMs = */ PrevFrameGPUTimeMs,
				/* BudgetTimingMs = */ BudgetTimingMs);

			Heuristic.RefreshCurentFrameResolutionFraction_RenderThread();

			// Set a non insane value for internal checks to pass as if GRHISupportsGPUBusyTimeQueries == true.
			CurrentFrameInFlightIndex = 0;
		}
	}

	void ProcessEvent(FRHICommandList& RHICmdList, EDynamicResolutionStateEvent Event)
	{
		check(IsInRenderingThread());

		if (bUseTimeQueriesThisFrame)
		{
			InFlightFrameQueries& InFlightFrame = InFlightFrames[CurrentFrameInFlightIndex];

			FRHIPooledRenderQuery* QueryPtr = nullptr;
			switch (Event)
			{
			case EDynamicResolutionStateEvent::BeginDynamicResolutionRendering:
				QueryPtr = &InFlightFrame.BeginDynamicResolutionQuery; break;
			case EDynamicResolutionStateEvent::EndDynamicResolutionRendering:
				QueryPtr = &InFlightFrame.EndDynamicResolutionQuery; break;
			case EDynamicResolutionStateEvent::EndFrame:
				QueryPtr = &InFlightFrame.EndFrameQuery; break;
			default: check(0);
			}

			check(QueryPtr != nullptr);
			check(QueryPool.IsValid());
			*QueryPtr = QueryPool->AllocateQuery();
			RHICmdList.EndRenderQuery(QueryPtr->GetQuery());
		}

		// Clobber CurrentFrameInFlightIndex for internal checks.
		if (Event == EDynamicResolutionStateEvent::EndFrame)
		{
			CurrentFrameInFlightIndex = -1;
			bUseTimeQueriesThisFrame = false;

			DynamicRenderScaling::EndFrame();
		}
	}

	/// Called before object is to be deleted
	void Finalize()
	{
		check(IsInRenderingThread());

		// Wait for all queries to land.
		HandLandedQueriesToHeuristic(/* bWait = */ true);

		// Reset the frame properties
		InFlightFrames.Empty();
		QueryPool = nullptr;
	}


	// Heuristic's proxy.
	FDynamicResolutionHeuristicProxy Heuristic;


private:
	struct InFlightFrameQueries
	{
		// GPU queries.
		FRHIPooledRenderQuery BeginFrameQuery;
		FRHIPooledRenderQuery BeginDynamicResolutionQuery;
		FRHIPooledRenderQuery EndDynamicResolutionQuery;
		FRHIPooledRenderQuery EndFrameQuery;

		// Heuristic's history 
		uint64 HeuristicHistoryEntry;

		InFlightFrameQueries()
		{ 
			ResetValues();
		}

		/// Reset values
		void ResetValues()
		{
			HeuristicHistoryEntry = FDynamicResolutionHeuristicProxy::kInvalidEntryId;
			BeginFrameQuery.ReleaseQuery();
			BeginDynamicResolutionQuery.ReleaseQuery();
			EndDynamicResolutionQuery.ReleaseQuery();
			EndFrameQuery.ReleaseQuery();
		}
	};

	// Shared query pool for the frames in flight
	FRenderQueryPoolRHIRef QueryPool;

	// List of frame queries in flight.
	TArray<InFlightFrameQueries> InFlightFrames;

	// Current frame's in flight index.
	int32 CurrentFrameInFlightIndex;

	// Uses GPU busy time queries.
	bool bUseTimeQueriesThisFrame;


	void HandLandedQueriesToHeuristic(bool bWait)
	{
		check(IsInRenderingThread());
		check(GRHISupportsGPUTimestampBubblesRemoval || bWait);

		bool ShouldRefreshHeuristic = false;

		for (int32 i = 0; i < InFlightFrames.Num(); i++)
		{
			// If current in flight frame queries, ignore them since have not called EndRenderQuery().
			if (i == CurrentFrameInFlightIndex)
			{
				continue;
			}

			InFlightFrameQueries& InFlightFrame = InFlightFrames[i];

			// Results in microseconds.
			uint64 BeginFrameResult = 0;
			uint64 BeginDynamicResolutionResult = 0;
			uint64 EndDynamicResolutionResult = 0;
			uint64 EndFrameResult = 0;

			int32 LandingCount = 0;
			int32 QueryCount = 0;
			if (InFlightFrame.BeginFrameQuery.IsValid())
			{
				LandingCount += RHIGetRenderQueryResult(
					InFlightFrame.BeginFrameQuery.GetQuery(), BeginFrameResult, bWait) ? 1 : 0;
				QueryCount += 1;
			}

			if (InFlightFrame.BeginDynamicResolutionQuery.IsValid())
			{
				LandingCount += RHIGetRenderQueryResult(
					InFlightFrame.BeginDynamicResolutionQuery.GetQuery(), BeginDynamicResolutionResult, bWait) ? 1 : 0;
				QueryCount += 1;
			}

			if (InFlightFrame.EndDynamicResolutionQuery.IsValid())
			{
				LandingCount += RHIGetRenderQueryResult(
					InFlightFrame.EndDynamicResolutionQuery.GetQuery(), EndDynamicResolutionResult, bWait) ? 1 : 0;
				QueryCount += 1;
			}

			if (InFlightFrame.EndFrameQuery.IsValid())
			{
				LandingCount += RHIGetRenderQueryResult(
					InFlightFrame.EndFrameQuery.GetQuery(), EndFrameResult, bWait) ? 1 : 0;
				QueryCount += 1;
			}

			check(QueryCount == 0 || QueryCount == 4);

			// If all queries have landed, then hand the results to the heuristic.
			if (LandingCount == 4)
			{
				Heuristic.CommitPreviousFrameGPUTimings_RenderThread(
					InFlightFrame.HeuristicHistoryEntry,
					/* TotalFrameGPUBusyTimeMs = */ TimeStampQueryResultToMiliSeconds(EndFrameResult - BeginFrameResult),
					/* DynamicResolutionGPUBusyTimeMs = */ TimeStampQueryResultToMiliSeconds(EndDynamicResolutionResult - BeginDynamicResolutionResult),
					/* bGPUTimingsHaveCPUBubbles = */ !GRHISupportsGPUTimestampBubblesRemoval);

				// Reset this in-flight frame queries to be reused.
				InFlightFrame.ResetValues();

				ShouldRefreshHeuristic = true;
			}
		}

		// Refresh the heuristic.
		if (ShouldRefreshHeuristic)
		{
			Heuristic.RefreshCurentFrameResolutionFraction_RenderThread();
		}
	}

	void FindNewInFlightIndex()
	{
		check(IsInRenderingThread());
		check(CurrentFrameInFlightIndex == -1);

		for (int32 i = 0; i < InFlightFrames.Num(); i++)
		{
			auto& InFlightFrame = InFlightFrames[i];
			if (!InFlightFrame.BeginFrameQuery.IsValid())
			{
				CurrentFrameInFlightIndex = i;
				break;
			}
		}

		// Allocate a new in-flight frame in the unlikely event.
		if (CurrentFrameInFlightIndex == -1)
		{
			CurrentFrameInFlightIndex = InFlightFrames.Add(InFlightFrameQueries());
		}
	}
};


/**
 * Engine's default dynamic resolution driver for view families.
 */
class FDefaultDynamicResolutionDriver : public ISceneViewFamilyScreenPercentage
{
public:

	FDefaultDynamicResolutionDriver(FDefaultDynamicResolutionStateProxy* InProxy, const FSceneViewFamily& InViewFamily)
		: Proxy(InProxy)
		, ViewFamily(InViewFamily)
	{
		check(IsInGameThread());
	}

	virtual DynamicRenderScaling::TMap<float> GetResolutionFractionsUpperBound() const override
	{
		DynamicRenderScaling::TMap<float> UpperBounds = Proxy->Heuristic.GetResolutionFractionUpperBounds();
		if (!ViewFamily.EngineShowFlags.ScreenPercentage)
		{
			UpperBounds[GDynamicPrimaryResolutionFraction] = 1.0f;
		}

		return UpperBounds;
	}

	virtual ISceneViewFamilyScreenPercentage* Fork_GameThread(const class FSceneViewFamily& ForkedViewFamily) const override
	{
		check(IsInGameThread());

		return new FDefaultDynamicResolutionDriver(Proxy, ForkedViewFamily);
	}

	virtual DynamicRenderScaling::TMap<float> GetResolutionFractions_RenderThread() const override
	{
		check(IsInRenderingThread());

		DynamicRenderScaling::TMap<float> ResolutionFractions = Proxy->Heuristic.QueryCurentFrameResolutionFractions();
		if (!ViewFamily.EngineShowFlags.ScreenPercentage)
		{
			ResolutionFractions[GDynamicPrimaryResolutionFraction] = 1.0f;
		}

		return ResolutionFractions;
	}

private:
	// Dynamic resolution proxy to use.
	FDefaultDynamicResolutionStateProxy* Proxy;

	// View family to take care of.
	const FSceneViewFamily& ViewFamily;

};


/**
 * Engine's default dynamic resolution state.
 */
class FDefaultDynamicResolutionState : public IDynamicResolutionState
{
public:
	FDefaultDynamicResolutionState()
		: Proxy(new FDefaultDynamicResolutionStateProxy())
	{
		check(IsInGameThread());
		bIsEnabled = false;
		bRecordThisFrame = false;
	}

	~FDefaultDynamicResolutionState() override
	{
		check(IsInGameThread());

		// Deletes the proxy on the rendering thread to make sure we don't delete before a recommand using it has finished.
		FDefaultDynamicResolutionStateProxy* P = Proxy;
		ENQUEUE_RENDER_COMMAND(DeleteDynamicResolutionProxy)(
			[P](class FRHICommandList&)
			{
				P->Finalize();
				delete P;
			});
	}


	// Implements IDynamicResolutionState

	virtual bool IsSupported() const override
	{
		// No VR platforms are officially supporting dynamic resolution with Engine default's dynamic resolution state.
		const bool bIsStereo = GEngine->StereoRenderingDevice.IsValid() ? GEngine->StereoRenderingDevice->IsStereoEnabled() : false;
		if (bIsStereo)
		{
			return false;
		}
		return GRHISupportsDynamicResolution;
	}

	virtual void ResetHistory() override
	{
		check(IsInGameThread());
		FDefaultDynamicResolutionStateProxy* P = Proxy;
		ENQUEUE_RENDER_COMMAND(DynamicResolutionResetHistory)(
			[P](class FRHICommandList&)
			{
				P->Reset();
			});

	}

	virtual void SetEnabled(bool bEnable) override
	{
		check(IsInGameThread());
		bIsEnabled = bEnable;
	}

	virtual bool IsEnabled() const override
	{
		check(IsInGameThread());
		return bIsEnabled;
	}

	virtual DynamicRenderScaling::TMap<float> GetResolutionFractionsApproximation() const override
	{
		check(IsInGameThread());
		return Proxy->Heuristic.GetResolutionFractionsApproximation_GameThread();
	}

	virtual DynamicRenderScaling::TMap<float> GetResolutionFractionsUpperBound() const override
	{
		check(IsInGameThread());
		return Proxy->Heuristic.GetResolutionFractionUpperBounds();
	}

	virtual void ProcessEvent(EDynamicResolutionStateEvent Event) override
	{
		check(IsInGameThread());

		if (Event == EDynamicResolutionStateEvent::BeginFrame)
		{
			check(bRecordThisFrame == false);
			bRecordThisFrame = bIsEnabled;
		}

		// Early return if not recording this frame.
		if (!bRecordThisFrame)
		{
			return;
		}

		if (Event == EDynamicResolutionStateEvent::BeginFrame)
		{
			// Query game thread time in milliseconds.
			float PrevGameThreadTimeMs = FPlatformTime::ToMilliseconds(GGameThreadTime);

			FDefaultDynamicResolutionStateProxy* P = Proxy;
			ENQUEUE_RENDER_COMMAND(DynamicResolutionBeginFrame)(
				[PrevGameThreadTimeMs, P](class FRHICommandList& RHICmdList)
			{
				P->BeginFrame(RHICmdList, PrevGameThreadTimeMs);
			});
		}
		else
		{
			// Forward event to render thread.
			FDefaultDynamicResolutionStateProxy* P = Proxy;
			ENQUEUE_RENDER_COMMAND(DynamicResolutionBeginFrame)(
				[P, Event](class FRHICommandList& RHICmdList)
			{
				P->ProcessEvent(RHICmdList, Event);
			});

			if (Event == EDynamicResolutionStateEvent::EndFrame)
			{
				// Only record frames that have a BeginFrame event.
				bRecordThisFrame = false;
			}
		}
	}

	virtual void SetupMainViewFamily(class FSceneViewFamily& ViewFamily) override
	{
		check(IsInGameThread());

		if (bIsEnabled)
		{
			ViewFamily.SetScreenPercentageInterface(new FDefaultDynamicResolutionDriver(Proxy, ViewFamily));
		}
	}

private:
	// Owned render thread proxy.
	FDefaultDynamicResolutionStateProxy* const Proxy;

	// Whether dynamic resolution is enabled.
	bool bIsEnabled;

	// Whether dynamic resolution is recording this frame.
	bool bRecordThisFrame;
};


//static
TSharedPtr< class IDynamicResolutionState > FDynamicResolutionHeuristicProxy::CreateDefaultState()
{
	return MakeShareable(new FDefaultDynamicResolutionState());
}
