// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNetStatsViewTooltip.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "TraceServices/Model/NetProfiler.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/NetworkingProfiler/ViewModels/NetEventNode.h"
#include "Insights/NetworkingProfiler/ViewModels/NetEventNodeHelper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SNetStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedPtr<SToolTip> SNetStatsViewTooltip::GetTableTooltip(const Insights::FTable& Table)
{
	TSharedPtr<SToolTip> ColumnTooltip =
		SNew(SToolTip)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(Table.GetDisplayName())
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(Table.GetDescription())
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			]
		];

	return ColumnTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SToolTip> SNetStatsViewTooltip::GetColumnTooltip(const Insights::FTableColumn& Column)
{
	TSharedPtr<SToolTip> ColumnTooltip =
		SNew(SToolTip)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(Column.GetTitleName())
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(Column.GetDescription())
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			]
		];

	return ColumnTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SToolTip> SNetStatsViewTooltip::GetRowTooltip(const TSharedPtr<FNetEventNode> NetEventNodePtr)
{
	const Trace::FNetProfilerAggregatedStats& Stats = NetEventNodePtr->GetAggregatedStats();

	const FText InstanceCountText = FText::AsNumber(Stats.InstanceCount);

	const FText TotalInclusiveSizeText = FText::AsNumber(Stats.TotalInclusive);
	const FText MaxInclusiveSizeText = FText::AsNumber(Stats.MaxInclusive);
	const FText AvgInclusiveSizeText = FText::AsNumber(Stats.AverageInclusive);

	const FText TotalExclusiveSizeText = FText::AsNumber(Stats.TotalExclusive);
	const FText MaxExclusiveSizeText = FText::AsNumber(Stats.MaxExclusive);
	//const FText AvgExclusiveSizeText = FText::AsNumber(Stats.AverageExclusive);
	const FText AvgExclusiveSizeText;

	TSharedPtr<SGridPanel> GridPanel;
	TSharedPtr<SHorizontalBox> HBox;

	TSharedPtr<SToolTip> TableCellTooltip =
		SNew(SToolTip)
		[
			SAssignNew(HBox, SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SGridPanel)

					// Event Type Index: [Index]
					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Id", "Event Type Index:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(NetEventNodePtr->GetEventTypeIndex()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					// Name: [Name]
					+ SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Name", "Name:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.WrapTextAt(512.0f)
						.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
						.Text(FText::FromName(NetEventNodePtr->GetName()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					//// Group: [MetaGroupName]
					//+ SGridPanel::Slot(0, 2)
					//.Padding(2.0f)
					//[
					//	SNew(STextBlock)
					//	.Text(LOCTEXT("TT_Group", "Group:"))
					//	.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					//]
					//+ SGridPanel::Slot(1, 2)
					//.Padding(2.0f)
					//[
					//	SNew(STextBlock)
					//	.WrapTextAt(512.0f)
					//	.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
					//	.Text(FText::FromName(NetEventNodePtr->GetMetaGroupName()))
					//	.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					//]

					// Net Event Type: [Type]
					+ SGridPanel::Slot(0, 3)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Type", "Type:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 3)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(NetEventNodeTypeHelper::ToText(NetEventNodePtr->GetType()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SGridPanel)

					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_NumInstances", "Num Instances:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(InstanceCountText)
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SAssignNew(GridPanel, SGridPanel)

					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_InclusiveSize", "Inclusive"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(2, 0)
					.Padding(FMargin(8.0f, 2.0f, 2.0f, 2.0f))
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_ExclusiveSize", "Exclusive"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]

					// Stats are added here.
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]
			]
		];

	int32 Row = 1;
	AddStatsRow(GridPanel, Row, LOCTEXT("TT_TotalSize",   "Total Size:"),   TotalInclusiveSizeText, TotalExclusiveSizeText);
	AddStatsRow(GridPanel, Row, LOCTEXT("TT_MaxSize",     "Max Size:"),     MaxInclusiveSizeText,   MaxExclusiveSizeText);
	AddStatsRow(GridPanel, Row, LOCTEXT("TT_AverageSize", "Average Size:"), AvgInclusiveSizeText,   AvgExclusiveSizeText);

	return TableCellTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetStatsViewTooltip::AddStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value1, const FText& Value2)
{
	Grid->AddSlot(0, Row)
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(Name)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
		];

	Grid->AddSlot(1, Row)
		.Padding(2.0f)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Text(Value1)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		];

	Grid->AddSlot(2, Row)
		.Padding(FMargin(8.0f, 2.0f, 2.0f, 2.0f))
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Text(Value2)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		];

	Row++;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
