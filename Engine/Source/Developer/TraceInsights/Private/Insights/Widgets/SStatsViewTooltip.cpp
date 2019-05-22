// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStatsViewTooltip.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/StatsNode.h"
#include "Insights/ViewModels/StatsViewColumn.h"
#include "Insights/ViewModels/StatsNodeHelper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SStatsViewTooltip"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedPtr<SToolTip> SStatsViewTooltip::GetColumnTooltip(const FStatsViewColumn& Column)
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
				.Text(Column.TitleName)
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(Column.Description)
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			]
		];

	return ColumnTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SToolTip> SStatsViewTooltip::GetTableCellTooltip(const TSharedPtr<FStatsNode> StatsNodePtr)
{
	//const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	//const FSlateFontInfo DescriptionFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	//const FSlateFontInfo DescriptionFontB = FCoreStyle::GetDefaultFontStyle("Bold", 8);

	const FLinearColor DefaultColor(1.0f,1.0f,1.0f,1.0f);
	const FLinearColor ThreadColor(5.0f, 0.0f, 0.0f, 1.0f);
	const float Alpha = 0.0f;//StatsNodePtr->_FramePct * 0.01f;
	const FLinearColor ColorAndOpacity = FMath::Lerp(DefaultColor, ThreadColor,Alpha);

	const FText InstanceCountText = FText::AsNumber(StatsNodePtr->GetAggregatedStats().Count);

	FText SumText = StatsNodePtr->GetTextForAggregatedStatsSum();
	FText MinText = StatsNodePtr->GetTextForAggregatedStatsMin();
	FText MaxText = StatsNodePtr->GetTextForAggregatedStatsMax();
	FText AvgText = StatsNodePtr->GetTextForAggregatedStatsAverage();
	FText MedText = StatsNodePtr->GetTextForAggregatedStatsMedian();
	FText LowText = StatsNodePtr->GetTextForAggregatedStatsLowerQuartile();
	FText UppText = StatsNodePtr->GetTextForAggregatedStatsUpperQuartile();

	//TSharedPtr<SHorizontalBox> HBoxCaption;
	TSharedPtr<SGridPanel> GridPanel;
	TSharedPtr<SHorizontalBox> HBox;

	TSharedPtr<SToolTip> TableCellTooltip =
		SNew(SToolTip)
		[
			SAssignNew(HBox, SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SVerticalBox)

				//+SVerticalBox::Slot()
				//.AutoHeight()
				//.Padding(2.0f)
				//[
				//	SAssignNew(HBoxCaption, SHorizontalBox)
				//]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SGridPanel)

					// Id: [Id]
					+SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Id", "Id:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(StatsNodePtr->GetId()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					// Name: [Name]
					+SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Name", "Name:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromName(StatsNodePtr->GetName()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					//// Group: [MetaGroupName]
					//+SGridPanel::Slot(0, 2)
					//.Padding(2.0f)
					//[
					//	SNew(STextBlock)
					//	.Text(LOCTEXT("TT_Group", "Group:"))
					//	.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					//]
					//+SGridPanel::Slot(1, 2)
					//.Padding(2.0f)
					//[
					//	SNew(STextBlock)
					//	.Text(FText::FromName(StatsNodePtr->GetMetaGroupName()))
					//	.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					//]

					// Type: [Type]
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
						.Text(StatsNodeTypeHelper::ToName(StatsNodePtr->GetType()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]
				]

				+SVerticalBox::Slot()
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

				+SVerticalBox::Slot()
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

					// Aggregated stats are added here.
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f)
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]
			]
		];

	int32 Row = 1;
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_Sum",     "Sum:"),            SumText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_Max",     "Max:"),            MaxText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_UpperQ",  "Upper Quartile:"), UppText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_Average", "Average:"),        AvgText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_Median",  "Median:"),         MedText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_LowerQ",  "Lower Quartile:"), LowText);
	AddAggregatedStatsRow(GridPanel, Row, LOCTEXT("TT_Min",     "Min:"),            MinText);

	/*
	//TODO: We need Stats hierarchy (not the grouping hierarchy)!
	const bool bHasParent = StatsNodePtr->GetStats()->GetParent().IsValid();
	const bool bHasChildren = StatsNodePtr->GetStats()->GetChildren().Num() > 0;

	if (bHasParent)
	{
		const FText ParentName = FText::FromName(StatsNodePtr->GetGroupPtr()->GetName());
		HBoxCaption->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(ParentName)
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
			];

		HBoxCaption->AddSlot()
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("BreadcrumbTrail.Delimiter"))
			];
	}

	const FText StatsName = FText::FromName(StatsNodePtr->GetName());
	HBoxCaption->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(StatsName)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.CaptionBold"))
		];

	if (bHasChildren)
	{
		typedef TKeyValuePair<float, FName> FEventNameAndPct;
		TArray<FEventNameAndPct> MinimalChildren;

		const TArray<FStatsNodePtr>& Children = StatsNodePtr->GetChildren();
		for(int32 ChildIndex = 0; ChildIndex < Children.Num(); ChildIndex++)
		{
			const FStatsNodePtr Child = Children[ChildIndex];
			float Percent = static_cast<float>(Child->GetStats().TotalInclusiveTime / StatsNodePtr->GetStats().TotalInclusiveTime * 100.0);
			MinimalChildren.Add(FEventNameAndPct(Child->GetStats().TotalInclusiveTime, Child->GetName()));
		}

		struct FCompareByFloatDescending
		{
			FORCEINLINE bool operator()(const FEventNameAndPct& A, const FEventNameAndPct& B) const
			{
				return A.Key > B.Key;
			}
		};
		MinimalChildren.Sort(FCompareByFloatDescending());

		FString ChildrenNames;
		const int32 NumChildrenToDisplay = FMath::Min(MinimalChildren.Num(), 3);
		for(int32 SortedChildIndex = 0; SortedChildIndex < NumChildrenToDisplay; SortedChildIndex++)
		{
			const FEventNameAndPct& MinimalChild = MinimalChildren[SortedChildIndex];
			ChildrenNames += FString::Printf(TEXT("%s (%.1f %%)"), *MinimalChild.Value.ToString(), MinimalChild.Key);

			const bool bAddDelimiter = SortedChildIndex < NumChildrenToDisplay - 1;
			if (bAddDelimiter)
			{
				ChildrenNames += TEXT(", ");
			}
		}

		HBoxCaption->AddSlot()
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("BreadcrumbTrail.Delimiter"))
			];

		HBoxCaption->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(ChildrenNames))
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Caption"))
			];
	}
	*/

	return TableCellTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SToolTip> SStatsViewTooltip::GetTooltip()
{
	if (Session.IsValid())
	{
		const TSharedRef<SGridPanel> ToolTipGrid = SNew(SGridPanel);
		int32 CurrentRowPos = 0;

		AddHeader(ToolTipGrid, CurrentRowPos);
		AddDescription(ToolTipGrid, CurrentRowPos);

		AddNoDataInformation(ToolTipGrid, CurrentRowPos);

		return SNew(SToolTip)
			[
				ToolTipGrid
			];
	}
	else
	{
		return SNew(SToolTip)
			.Text(LOCTEXT("NotImplemented", "Tooltip for multiple profiler instances has not been implemented yet"));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsViewTooltip::AddNoDataInformation(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos)
		.Padding(2.0f)
		.ColumnSpan(3)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
			.Text(LOCTEXT("NoDataAvailable", "N/A"))
		];
	RowPos++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsViewTooltip::AddHeader(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	const FString InstanceName = Session->GetName();

	Grid->AddSlot(0, RowPos++)
		.Padding(2.0f)
		.ColumnSpan(3)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
			.Text(LOCTEXT("StatInstance", "Stat information for profiler instance"))
		];

	Grid->AddSlot(0, RowPos++)
		.Padding(2.0f)
		.ColumnSpan(3)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.Text(FText::FromString(InstanceName))
		];

	AddSeparator(Grid, RowPos);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsViewTooltip::AddDescription(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	/*
	const FProfilerStat& ProfilerStat = Session->GetMetaData()->GetStatsById(StatsId);
	const EStatsNodeType SampleType = Session->GetMetaData()->GetSampleTypeForStatsId(StatsId);
	const FSlateBrush* const StatIcon = SStatsViewHelper::GetIconForStatType(SampleType);

	Grid->AddSlot(0, RowPos)
	.Padding(2.0f)
	[
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
		.Text(LOCTEXT("GroupDesc","Group:"))
	];

	Grid->AddSlot(1, RowPos)
	.Padding(2.0f)
	.ColumnSpan(2)
	[
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		.Text(FText::FromName(ProfilerStat.OwningGroup().Name()))
	];
	RowPos++;

	Grid->AddSlot(0, RowPos)
	.Padding(2.0f)
	[
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
		.Text(LOCTEXT("NameDesc","Name:"))
	];

	Grid->AddSlot(1, RowPos)
	.Padding(2.0f)
	.ColumnSpan(2)
	[
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		.Text(FText::FromName(ProfilerStat.Name()))
	];
	RowPos++;

	Grid->AddSlot(0, RowPos)
	.Padding(2.0f)
	[
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
		.Text(LOCTEXT("TypeDesc","Type:"))
	];

	Grid->AddSlot(1, RowPos)
	.Padding(2.0f)
	.ColumnSpan(2)
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SImage)
			.Image(StatIcon)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(EStatsNodeType::ToDescription(SampleType)))
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		]
	];
	RowPos++;

	AddSeparator(Grid, RowPos);
	*/
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsViewTooltip::AddSeparator(const TSharedRef<SGridPanel>& Grid, int32& RowPos)
{
	Grid->AddSlot(0, RowPos++)
		.Padding(2.0f)
		.ColumnSpan(3)
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsViewTooltip::AddAggregatedStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value)
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
			.Text(Value)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		];

	Row++;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
