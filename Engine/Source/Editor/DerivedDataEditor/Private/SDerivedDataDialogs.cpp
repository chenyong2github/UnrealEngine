// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDerivedDataDialogs.h"
#include "DerivedDataInformation.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Images/SImage.h"
#include "Styling/StyleColors.h"
#include "Math/UnitConversion.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Misc/ExpressionParser.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/FastDecimalFormat.h"


#define LOCTEXT_NAMESPACE "DerivedDataCacheEditor"

static FString SingleDecimalFormat(double Value)
{
	const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(true)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(1);
	return FastDecimalFormat::NumberToString(Value, ExpressionParser::GetLocalizedNumberFormattingRules(), NumberFormattingOptions);
};

void SDerivedDataRemoteStoreDialog::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 20, 0, 0)
		.Expose(GridSlot)
		[
			GetGridPanel()
		]
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDerivedDataRemoteStoreDialog::UpdateGridPanels));
}

EActiveTimerReturnType SDerivedDataRemoteStoreDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*GridSlot)
	[
		GetGridPanel()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SDerivedDataRemoteStoreDialog::GetGridPanel()
{
	TArray<FDerivedDataCacheResourceStat> DDCResourceStats;

	// Grab the latest resource stats
	GetDerivedDataCacheRef().GatherResourceStats(DDCResourceStats);

	FDerivedDataCacheResourceStat DDCResourceStatsTotal(TEXT("Total"));

	// Accumulate Totals
	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{
		DDCResourceStatsTotal.Accumulate(Stat);
	}

	const int64 TotalCount = DDCResourceStatsTotal.LoadCount + DDCResourceStatsTotal.BuildCount;
	const double Efficiency = TotalCount > 0 ? (double)DDCResourceStatsTotal.LoadCount / TotalCount : 0.0;

	const double DownloadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(true, false), EUnit::Bytes, EUnit::Megabytes);
	const double UploadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(false, false), EUnit::Bytes, EUnit::Megabytes);
	
	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);
	
	int32 Row = 0;

	/*Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FStyleColors::Foreground)
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
			.ColorAndOpacity(FStyleColors::Foreground)
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
			.ColorAndOpacity(FStyleColors::Foreground)
			.Text(LOCTEXT("Built", "Built"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(5, 0))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildSizeMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;*/

	Panel->AddSlot(0, Row)
		[
		SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Text(LOCTEXT("Remote Storage", "Remote Storage"))

		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Connected", "Connected"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FDerivedDataInformation::GetHasRemoteCache()? TEXT("True") : TEXT("False")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Downloaded", "Downloaded"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(DownloadedBytesMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Uploaded", "Uploaded"))
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SingleDecimalFormat(UploadedBytesMB) + TEXT(" MB")))
			.Justification(ETextJustify::Right)
		];

	Row++;

	return Panel;
}



void SDerivedDataResourceUsageDialog::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 20, 0, 0)
			.Expose(GridSlot)
			[
				GetGridPanel()
			]
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDerivedDataResourceUsageDialog::UpdateGridPanels));
}

EActiveTimerReturnType SDerivedDataResourceUsageDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*GridSlot)
	[
		GetGridPanel()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SDerivedDataResourceUsageDialog::GetGridPanel()
{	
	
	TArray<FDerivedDataCacheResourceStat> DDCResourceStats;

	// Grab the resource stats
	GetDerivedDataCacheRef().GatherResourceStats(DDCResourceStats);

	// Sort results on asscending Load size
	DDCResourceStats.Sort([](const FDerivedDataCacheResourceStat& LHS, const FDerivedDataCacheResourceStat& RHS) { return LHS.BuildSizeMB > RHS.BuildSizeMB; });

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
		SNew(SGridPanel);

	int32 Row = 0;

	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;

	Panel->AddSlot(2, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Loaded", "Loaded"))
		.Justification(ETextJustify::Center)
		];


	Panel->AddSlot(5, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Built", "Built"))
		.Justification(ETextJustify::Center)
		];

	Row++;

	Panel->AddSlot(0, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Asset", "Asset"))
		];

	Panel->AddSlot(1, Row)
		[
	
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Count", "Count"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(2, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(3, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Size (MB)", "Size (MB)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(4, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Count", "Count"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(5, Row)
		[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
		.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(6, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Size (MB)", "Size (MB)"))
		.Justification(ETextJustify::Center)
		];

	Row++;


	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{	
		Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(Stat.AssetType))
			.Justification(ETextJustify::Left)
		];

		Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(FString::Printf(TEXT("%u"),Stat.LoadCount)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(2, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadTimeSec)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(3, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadSizeMB)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(4, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(FString::Printf(TEXT("%u"),Stat.BuildCount)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(5, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildTimeSec)))
			.Justification(ETextJustify::Center)
		];

		Panel->AddSlot(6, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildSizeMB)))
			.Justification(ETextJustify::Center)
		];

		Row++;
	}
	
	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(DDCResourceStatsTotal.AssetType))
			.ColorAndOpacity(FStyleColors::AccentWhite)
			.Justification(ETextJustify::Left)
		];

	Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(FString::Printf(TEXT("%u"), DDCResourceStatsTotal.LoadCount)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(2, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadTimeSec)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(3, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadSizeMB)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(4, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(FString::Printf(TEXT("%u"), DDCResourceStatsTotal.BuildCount)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(5, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildTimeSec)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	Panel->AddSlot(6, Row)
		[
			SNew(STextBlock)
			.Margin(FMargin(ColumnMargin, RowMargin))
			.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildSizeMB)))
			.ColorAndOpacity(FStyleColors::Foreground)
			.Justification(ETextJustify::Center)
		];

	return Panel;
}


void SDerivedDataCacheStatisticsDialog::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 20, 0, 0)
		.Expose(GridSlot)
		[
			GetGridPanel()
		]

	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDerivedDataCacheStatisticsDialog::UpdateGridPanels));
}

EActiveTimerReturnType SDerivedDataCacheStatisticsDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*GridSlot)
	[
		GetGridPanel()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
} 



TSharedRef<SWidget> SDerivedDataCacheStatisticsDialog::GetGridPanel()
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
		SNew(SGridPanel);

	int32 Row = 0;

	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Cache", "Cache"))
	];

	Panel->AddSlot(1, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Justification(ETextJustify::Center)
		.Text(LOCTEXT("Speed", "Speed"))
	];

	Panel->AddSlot(2, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Justification(ETextJustify::Center)
		.Text(LOCTEXT("HitPercentage", "Hit%"))
	];

	Panel->AddSlot(3, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Justification(ETextJustify::Center)
		.Text(LOCTEXT("Read", "Read"))
	];

	Panel->AddSlot(4, Row)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Justification(ETextJustify::Center)
		.Text(LOCTEXT("Write", "Write"))
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
				.Margin(FMargin(ColumnMargin, RowMargin))
				.Text(FText::FromString(Backend->GetDisplayName()))
			];

			Panel->AddSlot(1, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(LexToString(Backend->GetSpeedClass())))
			];

			const double HitPercentage = TotalRequests > 0 ? (100.0 * (TotalGets_Hit / (double)TotalRequests)) : 0.0;

			Panel->AddSlot(2, Row)
				.HAlign(HAlign_Right)
				[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(HitPercentage) + TEXT(" %")))
				];

			const double TotalGetMB = FUnitConversion::Convert(TotalGetBytes, EUnit::Bytes, EUnit::Megabytes);

			SumTotalGetMB += TotalGetMB;

			Panel->AddSlot(3, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(TotalGetMB) + TEXT(" MB")))
			];

			const double TotalPutMB = FUnitConversion::Convert(TotalPutBytes, EUnit::Bytes, EUnit::Megabytes);

			SumTotalPutMB += TotalPutMB;

			Panel->AddSlot(4, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.Justification(ETextJustify::Right)
				.Text(FText::FromString(SingleDecimalFormat(TotalPutMB) + TEXT(" MB")))
			];

			Row++;
		}
	}

	Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Total")))
			.Margin(FMargin(ColumnMargin, RowMargin))
			.ColorAndOpacity(FStyleColors::AccentWhite)
			.Justification(ETextJustify::Left)	
		];

	Panel->AddSlot(3, Row)
		.HAlign(HAlign_Right)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin))
		.Justification(ETextJustify::Right)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalGetMB) + TEXT(" MB")))
		];

	Panel->AddSlot(4, Row)
		.HAlign(HAlign_Right)
		[
		SNew(STextBlock)
		.Margin(FMargin(ColumnMargin, RowMargin))
		.Justification(ETextJustify::Right)
		.ColorAndOpacity(FStyleColors::Foreground)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalPutMB) + TEXT(" MB")))
		];

	return Panel;
}

#undef LOCTEXT_NAMESPACE