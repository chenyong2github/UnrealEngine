// Copyright Epic Games, Inc. All Rights Reserved.

#include "STableTreeViewTooltip.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

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

TSharedPtr<SToolTip> STableTreeViewTooltip::GetRowTooltip(const TSharedPtr<FTableTreeNode> TreeNodePtr)
{
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

					// Id: [Id]
					+ SGridPanel::Slot(0, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Id", "Id:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 0)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(TreeNodePtr->GetId()))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					// Item Type: [Type]
					+ SGridPanel::Slot(0, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Type", "Item Type:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 1)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(TreeNodePtr->IsGroup() ? LOCTEXT("TT_Type_Group", "Group Node") : LOCTEXT("TT_Type_TableRow", "Table Row"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
					]

					// Item Name: [Name]
					+ SGridPanel::Slot(0, 2)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TT_Name", "Item Name:"))
						.TextStyle(FEditorStyle::Get(), TEXT("Profiler.TooltipBold"))
					]
					+ SGridPanel::Slot(1, 2)
					.Padding(2.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromName(TreeNodePtr->GetName()))
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

					// Values for each table column are added here.
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

	TSharedPtr<FTable> Table = TreeNodePtr->GetParentTable().Pin();
	if (Table.IsValid())
	{
		int32 Row = 0;
		for (const TSharedPtr<FTableColumn>& Column : Table->GetColumns())
		{
			if (!Column->IsHierarchy())
			{
				FText Name = FText::Format(LOCTEXT("TooltipValueFormat", "{0}:"), Column->GetTitleName());
				AddGridRow(GridPanel, Row, Name, Column->GetValueAsTooltipText(*TreeNodePtr));
			}
		}
	}

	return TableCellTooltip;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STableTreeViewTooltip::AddGridRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value)
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
		[
			SNew(STextBlock)
			.Text(Value)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
		];

	Row++;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
