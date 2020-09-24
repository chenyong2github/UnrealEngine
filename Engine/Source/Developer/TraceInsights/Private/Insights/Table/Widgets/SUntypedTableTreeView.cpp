// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUntypedTableTreeView.h"

#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/Log.h"
#include "Insights/Table/ViewModels/UntypedTable.h"

#define LOCTEXT_NAMESPACE "SUntypedTableTreeView"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

SUntypedTableTreeView::SUntypedTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SUntypedTableTreeView::~SUntypedTableTreeView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SUntypedTableTreeView::Construct(const FArguments& InArgs, TSharedPtr<Insights::FUntypedTable> InTablePtr)
{
	ConstructWidget(InTablePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SUntypedTableTreeView::Reset()
{
	//...
	STableTreeView::Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SUntypedTableTreeView::UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> SourceTable)
{
	//check(Table->Is<Insights::FUntypedTable>());
	TSharedPtr<Insights::FUntypedTable> UntypedTable = StaticCastSharedPtr<Insights::FUntypedTable>(Table);

	if (UntypedTable->UpdateSourceTable(SourceTable))
	{
		RebuildColumns();
	}

	RebuildTree(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SUntypedTableTreeView::RebuildTree(bool bResync)
{
	FStopwatch SyncStopwatch;
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (bResync)
	{
		TableTreeNodes.Empty();
	}

	const int32 PreviousNodeCount = TableTreeNodes.Num();

	//check(Table->Is<Insights::FUntypedTable>());
	TSharedPtr<Insights::FUntypedTable> UntypedTable = StaticCastSharedPtr<Insights::FUntypedTable>(Table);

	TSharedPtr<Trace::IUntypedTable> SourceTable = UntypedTable->GetSourceTable();
	TSharedPtr<Trace::IUntypedTableReader> TableReader = UntypedTable->GetTableReader();

	SyncStopwatch.Start();
	if (Session.IsValid() && SourceTable.IsValid() && TableReader.IsValid())
	{
		const int32 TotalRowCount = SourceTable->GetRowCount();
		if (TotalRowCount != TableTreeNodes.Num())
		{
			TableTreeNodes.Empty(TotalRowCount);
			FName BaseNodeName(TEXT("row"));
			for (int32 RowIndex = 0; RowIndex < TotalRowCount; ++RowIndex)
			{
				TableReader->SetRowIndex(RowIndex);
				FName NodeName(BaseNodeName, RowIndex + 1);
				FTableTreeNodePtr NodePtr = MakeShared<FTableTreeNode>(NodeName, Table, RowIndex);
				NodePtr->SetDefaultSortOrder(RowIndex + 1);
				TableTreeNodes.Add(NodePtr);
			}
			ensure(TableTreeNodes.Num() == TotalRowCount);
		}
	}
	SyncStopwatch.Stop();

	if (bResync || TableTreeNodes.Num() != PreviousNodeCount)
	{
		// Save selection.
		TArray<FTableTreeNodePtr> SelectedItems;
		TreeView->GetSelectedItems(SelectedItems);

		UpdateTree();

		TreeView->RebuildList();

		// Restore selection.
		if (SelectedItems.Num() > 0)
		{
			TreeView->ClearSelection();
			for (FTableTreeNodePtr& NodePtr : SelectedItems)
			{
				NodePtr = GetNodeByTableRowIndex(NodePtr->GetRowIndex());
			}
			SelectedItems.RemoveAll([](const FTableTreeNodePtr& NodePtr) { return !NodePtr.IsValid(); });
			if (SelectedItems.Num() > 0)
			{
				TreeView->SetItemSelection(SelectedItems, true);
				TreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	if (TotalTime > 0.01)
	{
		const double SyncTime = SyncStopwatch.GetAccumulatedTime();
		UE_LOG(TraceInsights, Log, TEXT("[Table] Tree view rebuilt in %.3fs (%.3fs + %.3fs) --> %d rows (%d added)"),
			TotalTime, SyncTime, TotalTime - SyncTime, TableTreeNodes.Num(), TableTreeNodes.Num() - PreviousNodeCount);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
