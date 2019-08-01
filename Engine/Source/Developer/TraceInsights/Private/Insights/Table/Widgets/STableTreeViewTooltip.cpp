// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STableTreeViewTooltip.h"

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
#include "Insights/Table/ViewModels/TableTreeNode.h"
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "STableTreeView"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedPtr<SToolTip> STableTreeViewTooltip::GetTableTooltip(const FTable& Table)
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

TSharedPtr<SToolTip> STableTreeViewTooltip::GetColumnTooltip(const FTableColumn& Column)
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

TSharedPtr<SToolTip> STableTreeViewTooltip::GetCellTooltip(const TSharedPtr<FTableTreeNode> TreeNodePtr, const TSharedPtr<FTableColumn> ColumnPtr)
{
	const FLinearColor TextColor(0.8f, 0.8f, 0.8f, 1.0f);
	const FLinearColor ValueColor(1.0f, 1.0f, 1.0f, 1.0f);

	//const int32 NumDigits = 5;
	//const FText TimeText = FText::FromString(TimeUtils::FormatTimeAuto(TotalTime));
	//const FText TimeText = FText::FromString(TimeUtils::FormatTimeMs(Time, NumDigits, true));

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
						.Text(FText::AsNumber(TreeNodePtr->GetId()))
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
						.Text(FText::FromName(TreeNodePtr->GetName()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

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
						.Text(FText::FromName(TreeNodePtr->GetTypeId()))
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
			]
		];

	return TableCellTooltip;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
