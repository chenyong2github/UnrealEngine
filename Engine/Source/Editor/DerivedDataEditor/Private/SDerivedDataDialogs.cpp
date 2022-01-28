// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDerivedDataDialogs.h"
#include "Algo/Sort.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataInformation.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "Misc/ExpressionParser.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DerivedDataCacheEditor"

template <typename ValueType>
static FString ZeroDecimalFormat(ValueType Value)
{
	const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(true)
		.SetMinimumFractionalDigits(0)
		.SetMaximumFractionalDigits(0);
	return FastDecimalFormat::NumberToString(Value, ExpressionParser::GetLocalizedNumberFormattingRules(), NumberFormattingOptions);
}

FString SingleDecimalFormat(double Value)
{
	const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(true)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(1);
	return FastDecimalFormat::NumberToString(Value, ExpressionParser::GetLocalizedNumberFormattingRules(), NumberFormattingOptions);
}

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
		DDCResourceStatsTotal += Stat;
	}

	const int64 TotalCount = DDCResourceStatsTotal.LoadCount + DDCResourceStatsTotal.BuildCount;
	const double Efficiency = TotalCount > 0 ? (double)DDCResourceStatsTotal.LoadCount / TotalCount : 0.0;

	const double DownloadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(true, false), EUnit::Bytes, EUnit::Megabytes);
	const double UploadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(false, false), EUnit::Bytes, EUnit::Megabytes);

	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	int32 Row = 0;

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
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Text(FDerivedDataInformation::GetHasRemoteCache() ? LOCTEXT("True", "True") : LOCTEXT("False", "False"))
	];

	Row++;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Downloaded", "Downloaded"))
	];

	Panel->AddSlot(1, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Text(FText::FromString(SingleDecimalFormat(DownloadedBytesMB) + TEXT(" MiB")))
	];

	Row++;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Uploaded", "Uploaded"))
	];

	Panel->AddSlot(1, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Text(FText::FromString(SingleDecimalFormat(UploadedBytesMB) + TEXT(" MiB")))
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

	// Sort results on descending build size, then descending load size, then ascending asset type.
	const auto CompareStats = [](const FDerivedDataCacheResourceStat& A, const FDerivedDataCacheResourceStat& B)
	{
		if (A.BuildSizeMB != B.BuildSizeMB)
		{
			return A.BuildSizeMB > B.BuildSizeMB;
		}
		if (A.LoadSizeMB != B.LoadSizeMB)
		{
			return A.LoadSizeMB > B.LoadSizeMB;
		}
		return A.AssetType.Compare(B.AssetType, ESearchCase::IgnoreCase) < 0;
	};
	Algo::Sort(DDCResourceStats, CompareStats);

	FDerivedDataCacheResourceStat DDCResourceStatsTotal(TEXT("Total"));

	// Accumulate Totals
	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{
		DDCResourceStatsTotal += Stat;
	}

	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	int32 Row = 0;

	const float RowMargin = 0.0f;
	const float ColumnMargin = 10.0f;
	const FMargin TitleMargin(0.0f, 10.0f, ColumnMargin, 10.0f);
	const FMargin TitleMarginFirstColumn(ColumnMargin, 10.0f);
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FMargin DefaultMargin(0.0f, RowMargin, ColumnMargin, RowMargin);
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);

	Panel->AddSlot(2, Row)
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Margin(DefaultMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Loaded", "Loaded"))
	];

	Panel->AddSlot(5, Row)
	.HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Margin(DefaultMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Built", "Built"))
	];

	Row++;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMarginFirstColumn)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("ResourceType", "Resource Type"))
	];

	Panel->AddSlot(1, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Count", "Count"))
	];

	Panel->AddSlot(2, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
	];

	Panel->AddSlot(3, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Size (MiB)", "Size (MiB)"))
	];

	Panel->AddSlot(4, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Count", "Count"))
	];

	Panel->AddSlot(5, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Time (Sec)", "Time (Sec)"))
	];

	Panel->AddSlot(6, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Size (MiB)", "Size (MiB)"))
	];

	Row++;

	for (const FDerivedDataCacheResourceStat& Stat : DDCResourceStats)
	{	
		Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Margin(DefaultMarginFirstColumn)
			.Text(FText::FromString(Stat.AssetType))
		];

		Panel->AddSlot(1, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(ZeroDecimalFormat(Stat.LoadCount)))
		];

		Panel->AddSlot(2, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadTimeSec)))
		];

		Panel->AddSlot(3, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(Stat.LoadSizeMB)))
		];

		Panel->AddSlot(4, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(ZeroDecimalFormat(Stat.BuildCount)))
		];

		Panel->AddSlot(5, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildTimeSec)))
		];

		Panel->AddSlot(6, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(Stat.BuildSizeMB)))
		];

		Row++;
	}
	
	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMarginFirstColumn)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(DDCResourceStatsTotal.AssetType))
	];

	Panel->AddSlot(1, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(ZeroDecimalFormat(DDCResourceStatsTotal.LoadCount)))
	];

	Panel->AddSlot(2, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadTimeSec)))
	];

	Panel->AddSlot(3, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.LoadSizeMB)))
	];

	Panel->AddSlot(4, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(ZeroDecimalFormat(DDCResourceStatsTotal.BuildCount)))
	];

	Panel->AddSlot(5, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildTimeSec)))
	];

	Panel->AddSlot(6, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(DDCResourceStatsTotal.BuildSizeMB)))
	];

	return Panel;
}



void SDerivedDataCacheStatisticsDialog::Construct(const FArguments& InArgs)
{
	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0, 20, 0, 0)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Margin(TitleMargin)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Left)
				.Text(FText::FromString(GetDerivedDataCache()->GetGraphName()))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 5, 0, 0)
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
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
	TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node)
	{
		if (Node->Children.IsEmpty())
		{
			LeafUsageStats.Add(Node);
		}
	});

	TSharedRef<SGridPanel> Panel =
		SNew(SGridPanel);

	const float RowMargin = 0.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FMargin DefaultMarginFirstColumn(ColumnMargin, RowMargin);

#if ENABLE_COOK_STATS

	int32 Row = 0;

	const FMargin TitleMargin(0.0f, 10.0f, ColumnMargin, 10.0f);
	const FMargin TitleMarginFirstColumn(ColumnMargin, 10.0f);
	const FMargin DefaultMargin(0.0f, RowMargin, ColumnMargin, RowMargin);
	
	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMarginFirstColumn)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.Text(LOCTEXT("Cache", "Cache"))
		.ColorAndOpacity(TitleColor)
		.Text(LOCTEXT("CacheType", "Cache Type"))
	];

	Panel->AddSlot(1, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Location", "Location"))
	];

	Panel->AddSlot(2, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("HitPercentage", "Hit%"))
	];

	Panel->AddSlot(3, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Read", "Read (MiB)"))
	];

	Panel->AddSlot(4, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Write", "Write (MiB)"))
	];

	Panel->AddSlot(5, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Details", "Details"))
	];

	Row++;

	double SumTotalGetMB = 0.0;
	double SumTotalPutMB = 0.0;

	for (TSharedRef<const FDerivedDataCacheStatsNode> Node : LeafUsageStats)
	{
		if (Node->GetCacheType().Equals(TEXT("Memory")))
		{
			continue;
		}

		FDerivedDataCacheUsageStats Stats;

		for (const auto& KVP : Node->Stats)
		{
			Stats.Combine(KVP.Value);
		}

		const int64 TotalGetBytes = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		const int64 TotalPutBytes = Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);

		const int64 TotalGetHits = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		const int64 TotalGetMisses = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter);
		const int64 TotalRequests = TotalGetHits + TotalGetMisses;
		const double HitRate = TotalRequests > 0 ? 100.0 * TotalGetHits / TotalRequests : 0.0;

		const double TotalGetMB = FUnitConversion::Convert(TotalGetBytes, EUnit::Bytes, EUnit::Megabytes);
		const double TotalPutMB = FUnitConversion::Convert(TotalPutBytes, EUnit::Bytes, EUnit::Megabytes);

		SumTotalGetMB += TotalGetMB;
		SumTotalPutMB += TotalPutMB;

		Panel->AddSlot(0, Row)
		[
			SNew(STextBlock)
			.Margin(DefaultMarginFirstColumn)
			.Text(FText::FromString(Node->GetCacheType()))
		];

		Panel->AddSlot(1, Row)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(Node->IsLocal() ? LOCTEXT("Local", "Local") : LOCTEXT("Remote", "Remote"))
		];

		Panel->AddSlot(2, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(HitRate)))
		];

		Panel->AddSlot(3, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(TotalGetMB)))
		];

		Panel->AddSlot(4, Row)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(SingleDecimalFormat(TotalPutMB)))
		];

		Panel->AddSlot(5, Row)
		[
			SNew(STextBlock)
			.Margin(DefaultMargin)
			.Text(FText::FromString(Node->GetCacheName()))
		];

		Row++;
	}

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.Margin(TitleMarginFirstColumn)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(LOCTEXT("Total", "Total"))
	];

	Panel->AddSlot(3, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalGetMB)))
	];

	Panel->AddSlot(4, Row)
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Margin(TitleMargin)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Text(FText::FromString(SingleDecimalFormat(SumTotalPutMB)))
	];

#else
	Panel->AddSlot(0, 0)
	[
		SNew(STextBlock)
		.Margin(DefaultMarginFirstColumn)
		.ColorAndOpacity(TitleColor)
		.Font(TitleFont)
		.Justification(ETextJustify::Center)
		.Text(LOCTEXT("Disabled", "Cooking Stats Are Disabled For This Project"))
	];

#endif // ENABLE_COOK_STATS

	return Panel;
}

#undef LOCTEXT_NAMESPACE
