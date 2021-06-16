// Copyright Epic Games, Inc. All Rights Reserved.

#include "DDC/SDDCInformation.h"
#include "DerivedDataCacheInterface.h"
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
		.Expose(GridSlot)
		[
			GetComplexDataGrid()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([] { return FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? EVisibility::Collapsed : EVisibility::Visible; })
		
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SDDCInformation::GetSimpleCacheInformation)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("ShiftMoreInformation", "[Press Shift for more information]"))
			]
		]
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCInformation::UpdateComplexDataGrid));
}

EActiveTimerReturnType SDDCInformation::UpdateComplexDataGrid(double InCurrentTime, float InDeltaTime)
{
	(*GridSlot)
	[
		GetComplexDataGrid()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	return EActiveTimerReturnType::Continue;
}

FText SDDCInformation::GetSimpleCacheInformation() const
{
	TStringBuilder<1024> Builder;

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

	TMap<FDerivedDataBackendInterface::ESpeedClass, FDerivedDataCacheUsageStats> Stats_BySource;

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();
		FDerivedDataCacheUsageStats& Stats_FromSource = Stats_BySource.FindOrAdd(Backend->GetSpeedClass());

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();
		for (const auto& KVP : Usage->Stats)
		{
			Stats_FromSource.Combine(KVP.Value);
		}
	}

	if (Stats_BySource.Num() > 0)
	{
		int64 TotalUploadedBytes = 0;
		for (const auto& KVP : Stats_BySource)
		{
			const FDerivedDataCacheUsageStats& Stats = KVP.Value;

			const int64 TotalGetHits = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
			const int64 TotalGetMisses = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter);
			const int64 TotalGets = TotalGetHits + TotalGetMisses;

			const int64 TotalGetBytes = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);

			const int64 TotalPutHits = Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
			const int64 TotalPutMisses = Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter);
			const int64 TotalPuts = TotalPutHits + TotalPutMisses;

			TotalUploadedBytes += Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);

			const double LoadedMB = FUnitConversion::Convert(TotalGetBytes, EUnit::Bytes, EUnit::Megabytes);

			if (LoadedMB > 0)
			{
				Builder.Append(LexToString(KVP.Key));

				Builder.Append(TEXT(" "));
				Builder.Append(SingleDecimalFormat(LoadedMB));
				Builder.Append(TEXT(" MB"));

				Builder.Append(TEXT("\n"));
			}
		}

		Builder.Append(TEXT("\n"));

		const double GeneratedMB = FUnitConversion::Convert(TotalUploadedBytes, EUnit::Bytes, EUnit::Megabytes);

		Builder.Append(TEXT("Generated "));
		Builder.Append(SingleDecimalFormat(GeneratedMB));
		Builder.Append(TEXT(" MB"));
	}

	return FText::FromString(Builder.ToString());
}

TSharedRef<SWidget> SDDCInformation::GetComplexDataGrid()
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
		.Visibility_Lambda([] { return FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? EVisibility::Visible : EVisibility::Collapsed; });

	int32 Row = 0;

	Panel->AddSlot(0, Row)
	[
		SNew(STextBlock)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text(LOCTEXT("DDC", "DDC"))
	];

	Panel->AddSlot(1, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text(LOCTEXT("Speed", "Speed"))
	];

	Panel->AddSlot(2, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text(LOCTEXT("Loaded", "Loaded"))
	];

	Panel->AddSlot(3, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text(LOCTEXT("HitPercentage", "Hit%"))
	];

	Panel->AddSlot(4, Row)
	[
		SNew(STextBlock)
		.Margin(FMargin(5, 0))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.12"))
		.Text(FEditorFontGlyphs::Pencil)
	];

	Row++;

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

		const int64 TotalGets_Hit = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		const int64 TotalGets_Miss = Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter);
		const int64 TotalGets = TotalGets_Hit + TotalGets_Miss;

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
				.Margin(FMargin(5, 0))
				.Text(FText::FromString(LexToString(Backend->GetSpeedClass())))
			];

			const double TotalGetMB = FUnitConversion::Convert(TotalGetBytes, EUnit::Bytes, EUnit::Megabytes);

			Panel->AddSlot(2, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(5, 0))
				.Text(FText::FromString(SingleDecimalFormat(TotalGetMB) + TEXT(" MB")))
			];

			double HitPercentage = 100.0 * (TotalGets_Hit / (double)TotalGets);

			Panel->AddSlot(3, Row)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Margin(FMargin(5, 0))
				.Text(FText::FromString(SingleDecimalFormat(HitPercentage) + TEXT("%")))
			];

			Panel->AddSlot(4, Row)
			.HAlign(HAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(Backend->IsWritable() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			];

			Row++;
		}
	}

	return Panel;
}

#undef LOCTEXT_NAMESPACE