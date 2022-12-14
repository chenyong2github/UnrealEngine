// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphProfilingView.h"

#include "PCGComponent.h"
#include "PCGEditor.h"
#include "PCGEditorGraph.h"
#include "PCGEditorGraphNode.h"
#include "PCGGraph.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "SPCGEditorGraphProfilingView"

namespace PCGEditorGraphProfilingView
{
	const FText NoDataAvailableText = LOCTEXT("NoDataAvailableText", "N/A");
	
	/** Names of the columns in the attribute list */
	const FName NAME_Node = FName(TEXT("Node"));
	const FName NAME_PrepareDataTime = FName(TEXT("PrepareDataTime"));
	const FName NAME_AvgExecutionTime = FName(TEXT("AvgExecutionTime"));
	const FName NAME_MinExecutionTime = FName(TEXT("MinExecutionTime"));
	const FName NAME_MaxExecutionTime = FName(TEXT("MaxExecutionTime"));
	const FName NAME_MinExecutionFrameTime = FName(TEXT("MinFrameTime"));
	const FName NAME_MaxExecutionFrameTime = FName(TEXT("MaxFrameTime"));
	const FName NAME_MinNbExecutionFrames = FName(TEXT("MinNbExecutionFrames"));
	const FName NAME_MaxNbExecutionFrames = FName(TEXT("MaxNbExecutionFrames"));
	const FName NAME_StdExecutionTime = FName(TEXT("StdExecutionTime"));
	const FName NAME_TotalExecutionTime = FName(TEXT("TotalExecutionTime"));
	const FName NAME_NbCalls = FName(TEXT("NbCalls"));
	const FName NAME_NbExecutionFrames = FName(TEXT("NbExecutionFrames"));
	const FName NAME_PostExecuteTime = FName(TEXT("PostExecuteTime"));

	/** Labels of the columns */
	const FText TEXT_NodeLabel = LOCTEXT("NodeLabel", "Node");
	const FText TEXT_PrepareDataTimeLabel = LOCTEXT("PrepareDataTimeLabel", "PrepareData (ms)");
	const FText TEXT_PostExecuteTimeLabel = LOCTEXT("PrepareDataTimeLabel", "PrepareData (ms)");
	const FText TEXT_AvgExecutionTimeLabel = LOCTEXT("AvgExecutionTimeLabel", "Avg Time(ms)");
	const FText TEXT_MinExecutionTimeLabel = LOCTEXT("MinExecutionTimeLabel", "Min Time(ms)");
	const FText TEXT_MaxExecutionTimeLabel = LOCTEXT("MaxExecutionTimeLabel", "Max Time(ms)");
	const FText TEXT_MinExecutionFrameTimeLabel = LOCTEXT("MinExecutionFrameTimeLabel", "Min Frame Time(ms)");
	const FText TEXT_MaxExecutionFrameTimeLabel = LOCTEXT("MaxExecutionFrameTimeLabel", "Max Frame Time(ms)");
	const FText TEXT_StdExecutionTimeLabel = LOCTEXT("StdExecutionTimeLabel", "Std(ms)");
	const FText TEXT_TotalExecutionTimeLabel = LOCTEXT("TotalExecutionTimeLabel", "Total time(s)");
	const FText TEXT_NbCallsLabel = LOCTEXT("NbCallsLabel", "Calls");
	const FText TEXT_NbExecutionFramesLabel = LOCTEXT("NbExecutionFramesLabel", "Exec frames");
}

void SPCGProfilingListViewItemRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const PCGProfilingListViewItemPtr& Item)
{
	InternalItem = Item;

	SMultiColumnTableRow<PCGProfilingListViewItemPtr>::Construct(
		SMultiColumnTableRow::FArguments()
		.Style(FAppStyle::Get(), "DataTableEditor.CellListViewRow"),
		InOwnerTableView);
}

TSharedRef<SWidget> SPCGProfilingListViewItemRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	FText ColumnData = LOCTEXT("ColumnError", "Unrecognized Column");
	if (InternalItem.IsValid())
	{
		if (ColumnId == PCGEditorGraphProfilingView::NAME_Node)
		{
			ColumnData = FText::FromName(InternalItem->Name);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_NbCalls)
		{
			ColumnData = FText::AsNumber(InternalItem->NbCalls);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_NbExecutionFrames)
		{
			ColumnData = FText::AsNumber(InternalItem->NbExecutionFrames);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MinNbExecutionFrames)
		{
			ColumnData = FText::AsNumber(InternalItem->MinNbExecutionFrames);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MaxNbExecutionFrames)
		{
			ColumnData = FText::AsNumber(InternalItem->MaxNbExecutionFrames);
		}
		// For all other values, if we don't have data, just write "N/A"
		else if (!InternalItem->HasData)
		{
			ColumnData = PCGEditorGraphProfilingView::NoDataAvailableText;
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_AvgExecutionTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->AvgExecutionTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MinExecutionTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->MinExecutionTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MaxExecutionTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->MaxExecutionTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MinExecutionFrameTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->MinExecutionFrameTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_MaxExecutionFrameTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->MaxExecutionFrameTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_StdExecutionTime)
		{
			// In ms
			ColumnData = FText::AsNumber(InternalItem->StdExecutionTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_TotalExecutionTime)
		{
			// In s
			ColumnData = FText::AsNumber(InternalItem->ExecutionTime);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_PrepareDataTime)
		{
			ColumnData = FText::AsNumber(InternalItem->PrepareDataTime * 1000.0);
		}
		else if (ColumnId == PCGEditorGraphProfilingView::NAME_PostExecuteTime)
		{
			ColumnData = FText::AsNumber(InternalItem->PostExecuteTime * 1000.0);
		}
	}

	return SNew(STextBlock).Text(ColumnData);
}

void SPCGEditorGraphProfilingView::OnItemDoubleClicked(PCGProfilingListViewItemPtr Item)
{
	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (!PCGEditor.IsValid())
	{
		return;
	}

	if (!Item.IsValid() || !Item->EditorNode)
	{
		return;
	}

	PCGEditor->JumpToNode(Item->EditorNode);
}

void SPCGEditorGraphProfilingView::Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor)
{
	PCGEditorPtr = InPCGEditor;

	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (PCGEditor)
	{
		PCGEditorGraph = PCGEditor->GetPCGEditorGraph();
	}

	ListViewHeader = CreateHeaderRowWidget();

	const TSharedRef<SScrollBar> HorizontalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Horizontal)
		.Thickness(FVector2D(12.0f, 12.0f));

	const TSharedRef<SScrollBar> VerticalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.Thickness(FVector2D(12.0f, 12.0f));
	
	SAssignNew(ListView, SListView<PCGProfilingListViewItemPtr>)
		.ListItemsSource(&ListViewItems)
		.HeaderRow(ListViewHeader)
		.OnGenerateRow(this, &SPCGEditorGraphProfilingView::OnGenerateRow)
		.OnMouseButtonDoubleClick(this, &SPCGEditorGraphProfilingView::OnItemDoubleClicked)
		.AllowOverscroll(EAllowOverscroll::No)
		.ExternalScrollbar(VerticalScrollBar)
		.ConsumeMouseWheel(EConsumeMouseWheel::Always);
	
	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshButton", "Refresh"))
				.OnClicked(this, &SPCGEditorGraphProfilingView::Refresh)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ResetButton", "Reset"))
				.OnClicked(this, &SPCGEditorGraphProfilingView::ResetTimers)
			]
		]
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				.ExternalScrollbar(HorizontalScrollBar)
				+SScrollBox::Slot()
				[
					ListView->AsShared()
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				VerticalScrollBar
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				HorizontalScrollBar
			]
		]
	];

	Refresh();
}

TSharedRef<SHeaderRow> SPCGEditorGraphProfilingView::CreateHeaderRowWidget()
{
	return SNew(SHeaderRow)
		.ResizeMode(ESplitterResizeMode::FixedPosition)
		.CanSelectGeneratedColumn(true)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_Node)
		.ManualWidth(150)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_NodeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Left)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_Node)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_NbCalls)
		.ManualWidth(80)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_NbCallsLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_NbCalls)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_PrepareDataTime)
		.ManualWidth(125)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_PrepareDataTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_PrepareDataTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_NbExecutionFrames)
		.ManualWidth(80)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_NbExecutionFramesLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_NbExecutionFrames)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_MinExecutionFrameTime)
		.ManualWidth(130)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_MinExecutionFrameTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_MinExecutionFrameTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_MaxExecutionFrameTime)
		.ManualWidth(130)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_MaxExecutionFrameTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_MaxExecutionFrameTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_AvgExecutionTime)
		.ManualWidth(100)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_AvgExecutionTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_AvgExecutionTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_MinExecutionTime)
		.ManualWidth(100)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_MinExecutionTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_MinExecutionTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_MaxExecutionTime)
		.ManualWidth(100)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_MaxExecutionTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_MaxExecutionTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_StdExecutionTime)
		.ManualWidth(100)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_StdExecutionTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_StdExecutionTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader)
		+ SHeaderRow::Column(PCGEditorGraphProfilingView::NAME_TotalExecutionTime)
		.ManualWidth(100)
		.DefaultLabel(PCGEditorGraphProfilingView::TEXT_TotalExecutionTimeLabel)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Right)
		.SortMode(this, &SPCGEditorGraphProfilingView::GetColumnSortMode, PCGEditorGraphProfilingView::NAME_TotalExecutionTime)
		.OnSort(this, &SPCGEditorGraphProfilingView::OnSortColumnHeader);
}

void SPCGEditorGraphProfilingView::OnSortColumnHeader(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type NewSortMode)
{
	if (SortingColumn == ColumnId)
	{
		// Circling
		SortMode = EColumnSortMode::Type((SortMode + 1) % 3);
	}
	else
	{
		SortingColumn = ColumnId;
		SortMode = NewSortMode;
	}

	Refresh();
}

EColumnSortMode::Type SPCGEditorGraphProfilingView::GetColumnSortMode(const FName ColumnId) const
{
	if (SortingColumn != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return SortMode;
}

FReply SPCGEditorGraphProfilingView::ResetTimers()
{
	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (!PCGEditor.IsValid())
	{
		return FReply::Handled();
	}

	if (!PCGEditorGraph)
	{
		return FReply::Handled();
	}

	TArray<UPCGEditorGraphNode*> EditorNodes;
	PCGEditorGraph->GetNodesOfClass<UPCGEditorGraphNode>(EditorNodes);

	for (UPCGEditorGraphNode* PCGEditorNode : EditorNodes)
	{
		if (PCGEditorNode)
		{
			if (UPCGNode* PCGNode = PCGEditorNode->GetPCGNode())
			{
				if (UPCGSettings* Settings = PCGNode->GetSettings())
				{
					if (IPCGElement* Element = Settings->GetElement().Get())
					{
						Element->ResetTimers();
					}
				}
			}
		}
	}

	Refresh();

	return FReply::Handled();
}

FReply SPCGEditorGraphProfilingView::Refresh()
{
	ListViewItems.Empty();
	ListView->RequestListRefresh();

	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (!PCGEditor.IsValid())
	{
		return FReply::Handled();
	}

	if (!PCGEditorGraph)
	{
		return FReply::Handled();
	}

	TArray<UPCGEditorGraphNode*> EditorNodes;
	PCGEditorGraph->GetNodesOfClass<UPCGEditorGraphNode>(EditorNodes);
	ListViewItems.Reserve(EditorNodes.Num());

	for (const UPCGEditorGraphNode* PCGEditorNode : EditorNodes)
	{
		if (PCGEditorNode)
		{
			PCGProfilingListViewItemPtr ListViewItem = MakeShared<FPCGProfilingListViewItem>();
			ListViewItem->EditorNode = PCGEditorNode;
			ListViewItem->PCGNode = PCGEditorNode->GetPCGNode();

			if (ListViewItem->PCGNode)
			{
				ListViewItem->Name = ListViewItem->PCGNode->GetNodeTitle();

				if (const UPCGSettings* Settings = ListViewItem->PCGNode->GetSettings())
				{
					if (const IPCGElement* Element = Settings->GetElement().Get())
					{
						const TArray<IPCGElement::FCallTime>& Timers = Element->GetTimers();
						if (Timers.IsEmpty())
						{
							continue;
						}

						ListViewItem->MinExecutionTime = std::numeric_limits<double>::max();
						ListViewItem->MaxExecutionTime = std::numeric_limits<double>::min();
						ListViewItem->MinExecutionFrameTime = std::numeric_limits<double>::max();
						ListViewItem->MaxExecutionFrameTime = std::numeric_limits<double>::min();

						for(const IPCGElement::FCallTime& Timer : Timers)
						{
							ListViewItem->PrepareDataTime += Timer.PrepareDataTime;

							ListViewItem->MinExecutionFrameTime = FMath::Min(ListViewItem->MinExecutionFrameTime, Timer.MinExecutionFrameTime);
							ListViewItem->MaxExecutionFrameTime = FMath::Max(ListViewItem->MaxExecutionFrameTime, Timer.MaxExecutionFrameTime);

							ListViewItem->ExecutionTime += Timer.ExecutionTime;
							ListViewItem->MinExecutionTime = FMath::Min(ListViewItem->MinExecutionTime, Timer.ExecutionTime);
							ListViewItem->MaxExecutionTime = FMath::Max(ListViewItem->MaxExecutionTime, Timer.ExecutionTime);

							ListViewItem->NbExecutionFrames += Timer.ExecutionFrameCount;
							ListViewItem->MaxNbExecutionFrames = FMath::Max(ListViewItem->MaxNbExecutionFrames, Timer.ExecutionFrameCount);
							ListViewItem->MinNbExecutionFrames = FMath::Min(ListViewItem->MinNbExecutionFrames, Timer.ExecutionFrameCount);
						}

						ListViewItem->NbCalls = Timers.Num();
						ListViewItem->AvgExecutionTime = ListViewItem->ExecutionTime / ListViewItem->NbCalls;

						for (const IPCGElement::FCallTime& Timer : Timers)
						{
							ListViewItem->StdExecutionTime += FMath::Square(ListViewItem->AvgExecutionTime - Timer.ExecutionTime);
						}

						ListViewItem->StdExecutionTime = FMath::Sqrt(ListViewItem->StdExecutionTime / ListViewItem->NbCalls);
						ListViewItem->HasData = true;
					}
				}
			}

			ListViewItems.Add(ListViewItem);
		}
	}

	if (SortingColumn != NAME_None && SortMode != EColumnSortMode::None)
	{
		Algo::Sort(ListViewItems, [this](const PCGProfilingListViewItemPtr& A, const PCGProfilingListViewItemPtr& B)
			{
				bool isLess = false;
				if (SortingColumn == PCGEditorGraphProfilingView::NAME_Node)
				{
					isLess = A->Name.FastLess(B->Name);
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_PrepareDataTime)
				{
					isLess = A->PrepareDataTime < B->PrepareDataTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_AvgExecutionTime)
				{
					isLess = A->AvgExecutionTime < B->AvgExecutionTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MinExecutionTime)
				{
					isLess = A->MinExecutionTime < B->MinExecutionTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MaxExecutionTime)
				{
					isLess = A->MaxExecutionTime < B->MaxExecutionTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MinExecutionFrameTime)
				{
					isLess = A->MinExecutionFrameTime < B->MinExecutionFrameTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MaxExecutionFrameTime)
				{
					isLess = A->MaxExecutionFrameTime < B->MaxExecutionFrameTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_StdExecutionTime)
				{
					isLess = A->StdExecutionTime < B->StdExecutionTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_TotalExecutionTime)
				{
					isLess = A->ExecutionTime < B->ExecutionTime;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_NbCalls)
				{
					isLess = A->NbCalls < B->NbCalls;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_NbExecutionFrames)
				{
					isLess = A->NbExecutionFrames < B->NbExecutionFrames;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MaxNbExecutionFrames)
				{
					isLess = A->MaxNbExecutionFrames < B->MaxNbExecutionFrames;
				}
				else if (SortingColumn == PCGEditorGraphProfilingView::NAME_MinNbExecutionFrames)
				{
					isLess = A->MinNbExecutionFrames < B->MinNbExecutionFrames;
				}

				return SortMode == EColumnSortMode::Ascending ? isLess : !isLess;
			});
	}

	ListView->SetItemsSource(&ListViewItems);

	return FReply::Handled();
}

TSharedRef<ITableRow> SPCGEditorGraphProfilingView::OnGenerateRow(PCGProfilingListViewItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SPCGProfilingListViewItemRow, OwnerTable, Item);
}

#undef LOCTEXT_NAMESPACE
