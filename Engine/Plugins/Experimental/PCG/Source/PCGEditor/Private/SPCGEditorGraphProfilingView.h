// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/ITableRow.h"
#include "Widgets/Views/STableRow.h"

class STableViewBase;

class FPCGEditor;
class UPCGComponent;
class UPCGEditorGraphNode;
class UPCGEditorGraph;
class UPCGNode;

struct FPCGProfilingListViewItem
{
	const UPCGNode* PCGNode = nullptr;
	const UPCGEditorGraphNode* EditorNode = nullptr;

	FName Name = NAME_None;
	double PrepareDataTime = 0.0;
	double ExecutionTime = 0.0;
	double AvgExecutionTime = 0.0;
	double MinExecutionTime = 0.0;
	double MaxExecutionTime = 0.0;
	double MinExecutionFrameTime = 0.0;
	double MaxExecutionFrameTime = 0.0;
	double StdExecutionTime = 0.0;
	double PostExecuteTime = 0.0;
	int32 NbCalls = 0;
	double NbExecutionFrames = 0;
	int32 MaxNbExecutionFrames = 0;
	int32 MinNbExecutionFrames = 0;
	bool HasData = false;
};

typedef TSharedPtr<FPCGProfilingListViewItem> PCGProfilingListViewItemPtr;

class SPCGProfilingListViewItemRow : public SMultiColumnTableRow<PCGProfilingListViewItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SPCGProfilingListViewItemRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const PCGProfilingListViewItemPtr& Item);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override;

protected:
	PCGProfilingListViewItemPtr InternalItem;
};

class SPCGEditorGraphProfilingView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGEditorGraphProfilingView) { }
	SLATE_END_ARGS()

	~SPCGEditorGraphProfilingView();

	void Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor);

private:
	TSharedRef<SHeaderRow> CreateHeaderRowWidget();
	bool FillItem(const UPCGComponent* InComponent, const UPCGEditorGraphNode* InPCGEditorNode, const UPCGNode* InPCGNode, const FName& InName, FPCGProfilingListViewItem& OutItem);

	ECheckBoxState IsSubgraphExpanded() const;
	void OnSubgraphExpandedChanged(ECheckBoxState InNewState);

	void OnDebugObjectChanged(UPCGComponent* InPCGComponent);
	void OnGenerateUpdated(UPCGComponent* InPCGComponent);
	
	// Callbacks
	FReply Refresh();
	FReply ResetTimers();
	TSharedRef<ITableRow> OnGenerateRow(PCGProfilingListViewItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	void OnItemDoubleClicked(PCGProfilingListViewItemPtr Item);
	void OnSortColumnHeader(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type NewSortMode);
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;

	/** Pointer back to the PCG editor that owns us */
	TWeakPtr<FPCGEditor> PCGEditorPtr;

	/** Cached PCGGraph being viewed */
	UPCGEditorGraph* PCGEditorGraph = nullptr;

	/** Cached PCGComponent being viewed */
	TWeakObjectPtr<UPCGComponent> PCGComponent;

	TSharedPtr<SHeaderRow> ListViewHeader;
	TSharedPtr<SListView<PCGProfilingListViewItemPtr>> ListView;
	TArray<PCGProfilingListViewItemPtr> ListViewItems;

	// To allow sorting
	FName SortingColumn = NAME_None;
	EColumnSortMode::Type SortMode = EColumnSortMode::Type::None;

	bool bExpandSubgraph = true;
};
