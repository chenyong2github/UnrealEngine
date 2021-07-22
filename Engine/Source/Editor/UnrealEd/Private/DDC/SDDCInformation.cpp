// Copyright Epic Games, Inc. All Rights Reserved.

#include "DDC/SDDCInformation.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheUsageStats.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataBackendInterface.h"
#include "Math/UnitConversion.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Text.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Algo/Compare.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SDDCInformation"

static FString SingleDecimalFormat(double Value)
{
	const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(true)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(1);
	return FastDecimalFormat::NumberToString(Value, ExpressionParser::GetLocalizedNumberFormattingRules(), NumberFormattingOptions);
};

void SDDCInformation::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Expose(SummaryGridSlot)
		[
			GetSummaryGrid()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Expose(CacheGridSlot)
		[
			GetCacheGrid()
		]

		+ SVerticalBox::Slot()
		.AutoHeight().Padding(0, 20, 0, 0)
		.Expose(AssetGridSlot)
		[
			GetAssetGrid()
		]
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCInformation::UpdateGridPanels));
}

EActiveTimerReturnType SDDCInformation::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*SummaryGridSlot)
	[
		GetSummaryGrid()
	];

	(*CacheGridSlot)
	[
		GetCacheGrid()
	];

	(*AssetGridSlot)
	[
		GetAssetGrid()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SDDCInformation::GetAssetGrid()
{	
	TArray<FDerivedDataCacheResourceStat> DDCResourceStats;

	// Grab the resource stats
	GatherDerivedDataCacheResourceStats(DDCResourceStats);

	// Sort results on asscending Load size
	DDCResourceStats.Sort([](const FDerivedDataCacheResourceStat& LHS, const FDerivedDataCacheResourceStat& RHS) { return LHS.LoadSizeMB > RHS.LoadSizeMB; });

	FDerivedDataCacheResourceStat DDCResourceStatsTotal(TEXT("Total"));

	// Accumulate Totals
	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{
		DDCResourceStatsTotal.Accumulate(Stat);
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel)
		.Visibility_Lambda([] { return GetShowDetailedInformation() ? EVisibility::Visible : EVisibility::Collapsed;  });

	int32 Row = 0;

	Panel->AddSlot(2, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Loaded", "Loaded"))
		.Justification(ETextJustify::Center)
		];


	Panel->AddSlot(5, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Built", "Built"))
		.Justification(ETextJustify::Center)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Asset", "Asset"))
		];

	Panel->AddSlot(1, Row)
		[
	
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Count", "Count"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(2, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(3, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Size (MB)", "Size (MB)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(4, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Count", "Count"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(5, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(6, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Size (MB)", "Size (MB)"))
		.Justification(ETextJustify::Center)
		];

	Row++;


	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{	
		Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Stat.AssetType))
			.Justification(ETextJustify::Left)
		];

		Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%u"),Stat.LoadCount)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(2, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadTimeSec)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(3, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadSizeMB)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(4, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%u"),Stat.BuildCount)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(5, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildTimeSec)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(6, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildSizeMB)))
			.Justification(ETextJustify::Center)
		];

		Row++;
	}
	
	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DDCResourceStatsTotal.AssetType))
			.ColorAndOpacity(FStyleColors::AccentWhite)
			.Justification(ETextJustify::Left)
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%u"), DDCResourceStatsTotal.LoadCount)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(2, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadTimeSec)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(3, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadSizeMB)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(4, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%u"), DDCResourceStatsTotal.BuildCount)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(5, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildTimeSec)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(6, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildSizeMB)))
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Justification(ETextJustify::Center)
		];

	return Panel;
}


TSharedRef<SWidget> SDDCInformation::GetSummaryGrid()
{
	TArray<FDerivedDataCacheResourceStat> DDCResourceStats;

	// Grab the latest resource stats
	GatherDerivedDataCacheResourceStats(DDCResourceStats);

	FDerivedDataCacheResourceStat DDCResourceStatsTotal(TEXT("Total"));

	// Accumulate Totals
	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{
		DDCResourceStatsTotal.Accumulate(Stat);
	}

	const int64 TotalCount = DDCResourceStatsTotal.LoadCount + DDCResourceStatsTotal.BuildCount;
	const double Efficiency = TotalCount > 0 ? (double)DDCResourceStatsTotal.LoadCount / TotalCount : 0.0;

	const double DownloadedBytesMB = FUnitConversion::Convert(GetDDCSizeBytes(true, false), EUnit::Bytes, EUnit::Megabytes);
	const double UploadedBytesMB = FUnitConversion::Convert(GetDDCSizeBytes(false, false), EUnit::Bytes, EUnit::Megabytes);
	
	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel)
		.Visibility_Lambda([] { return GetShowDetailedInformation() == false ? EVisibility::Visible : EVisibility::Collapsed;  });

	int32 Row = 0;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Text(LOCTEXT("Efficiency", "Efficiency"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(Efficiency * 100.0f) + TEXT(" %")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Text(LOCTEXT("Loaded", "Loaded"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadSizeMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;
	
	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Text(LOCTEXT("Built", "Built"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildSizeMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Text(LOCTEXT("Downloaded", "Downloaded"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(DownloadedBytesMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::ForegroundHover)
			.Text(LOCTEXT("Uploaded", "Uploaded"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(UploadedBytesMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("ShiftMoreInformation", "[Hold Shift for more information]"))

		];

	Row++;

	return Panel;
}


TSharedRef<SWidget> SDDCInformation::GetCacheGrid()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
	});

	TSharedRef<SGridPanel> Panel = 
		SNew(SGridPanel)
		.Visibility_Lambda([] { return GetShowDetailedInformation()==true ? EVisibility::Visible : EVisibility::Collapsed; });

	int32 Row = 0;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(LOCTEXT("Cache", "Cache"))
	];

	Panel->AddSlot(1, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(10, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Justification(ETextJustify::Right)
		.Text(LOCTEXT("Speed", "Speed"))
	];

	Panel->AddSlot(2, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Justification(ETextJustify::Right)
		.Text(LOCTEXT("HitPercentage", "Hit%"))
	];

	Panel->AddSlot(3, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Justification(ETextJustify::Right)
		.Text(LOCTEXT("Read", "Read"))
	];

	Panel->AddSlot(4, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Justification(ETextJustify::Right)
		.Text(LOCTEXT("Write", "Write"))
		];

	Panel->AddSlot(5, Row)
	[
		SNew(SImage)
		.Image(FAppStyle::Get().GetBrush("Icons.Edit"))
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		
	];

	Row++;

	double SumTotalGetMB =0.0;
	double SumTotalPutMB = 0.0;

	for (TSharedRef<const FDerivedDataCacheStatsNode> Node : LeafUsageStats)
	{
		FDerivedDataCacheUsageStats Stats;

		const FDerivedDataBackendInterface* Backend = Node->GetBackendInterface();

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();
		for (const auto& KVP : Usage->Stats)
		{
			Stats.Combine(KVP.Value);
		}

		const int64 TotalGetBytes = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		const int64 TotalPutBytes = Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		const int64 TotalPrefetchBytes = Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);

		const int64 TotalGets_Hit = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		const int64 TotalGets_Miss = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter);

		const int64 TotalRequests = TotalGets_Hit + TotalGets_Miss;

		//if (TotalGetBytes > 0 || TotalPutBytes > 0)
		{
			Panel->AddSlot(0, Row)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Backend->GetDisplayName()))
			];

			Panel->AddSlot(1, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(10, 0))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(LexToString(Backend->GetSpeedClass())))
			];

			const double HitPercentage = TotalRequests > 0 ? (100.0 * (TotalGets_Hit / (double)TotalRequests)) : 0.0;

			Panel->AddSlot(2, Row)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Margin(FMargin(5, 0))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(HitPercentage) + TEXT(" %")))
				];

			const double TotalGetMB = FUnitConversion::Convert(TotalGetBytes, EUnit::Bytes, EUnit::Megabytes);

			SumTotalGetMB += TotalGetMB;

			Panel->AddSlot(3, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(5, 0))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(TotalGetMB) + TEXT(" MB")))
			];

			const double TotalPutMB = FUnitConversion::Convert(TotalPutBytes, EUnit::Bytes, EUnit::Megabytes);

			SumTotalPutMB += TotalPutMB;

			Panel->AddSlot(4, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(5, 0))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(TotalPutMB) + TEXT(" MB")))
			];

			Panel->AddSlot(5, Row)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Check"))
				.Visibility(Backend->IsWritable() ? EVisibility::Visible : EVisibility::Hidden)
			];

			Row++;
		}
	}

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Total")))
			.ColorAndOpacity(FStyleColors::AccentWhite)
			.Justification(ETextJustify::Left)	
		];

	Panel->AddSlot(3, Row)
		.HAlign(HAlign_Right)
		[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.Justification(ETextJustify::Right)
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalGetMB) + TEXT(" MB")))
		];

	Panel->AddSlot(4, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
		.Justification(ETextJustify::Right)
		.ColorAndOpacity(FStyleColors::ForegroundHover)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalPutMB) + TEXT(" MB")))
		];

	return Panel;
}

double SDDCInformation::GetDDCSizeBytes(bool bGet, bool bLocal)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	int64 TotalBytes = 0;

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if ((Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local) != bLocal)
			continue;

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();

		for (const auto& KVP : Usage->Stats)
		{
			const FDerivedDataCacheUsageStats& Stats = KVP.Value;

			if (bGet)
			{
				TotalBytes += Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
			}
			else
			{
				TotalBytes += Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
			}
		}
	}

	return TotalBytes;
}


double SDDCInformation::GetDDCTimeSeconds(bool bGet, bool bLocal)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	int64 TotalCycles = 0;

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if ((Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local) != bLocal)
			continue;

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();

		for (const auto& KVP : Usage->Stats)
		{
			const FDerivedDataCacheUsageStats& Stats = KVP.Value;

			if (bGet)
			{
				TotalCycles +=
					(Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));

				TotalCycles +=
					(Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
			}
			else
			{
				TotalCycles +=
					(Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
			}
		}
	}

	return (double)TotalCycles * FPlatformTime::GetSecondsPerCycle();
}

bool SDDCInformation::GetDDCHasLocalBackend()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if (Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local)
			return true;
	}

	return false;
}


bool SDDCInformation::GetDDCHasRemoteBackend()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if (Backend->GetSpeedClass() != FDerivedDataBackendInterface::ESpeedClass::Local)
			return true;
	}

	return false;
}

bool SDDCInformation::GetShowDetailedInformation()
{
	return FSlateApplication::Get().GetModifierKeys().IsShiftDown();
}


#undef LOCTEXT_NAMESPACE