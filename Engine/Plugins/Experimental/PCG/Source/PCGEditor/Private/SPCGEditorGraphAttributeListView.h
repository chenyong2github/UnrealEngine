// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

class FPCGEditor;
class UPCGComponent;
class UPCGNode;
struct FPCGPoint;

struct FPCGListViewItem
{
	int32 Index = INDEX_NONE;
	const FPCGPoint* PCGPoint = nullptr;
};

typedef TSharedPtr<FPCGListViewItem> PCGListviewItemPtr;

class SPCGListViewItemRow : public SMultiColumnTableRow<PCGListviewItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SPCGListViewItemRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const PCGListviewItemPtr& Item);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override;

protected:
	PCGListviewItemPtr InternalItem;
};

class SPCGEditorGraphAttributeListView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGEditorGraphAttributeListView) { }
	SLATE_END_ARGS()

	virtual ~SPCGEditorGraphAttributeListView();

	void Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor);

private:
	TSharedRef<SHeaderRow> CreateHeaderRowWidget() const;
	
	void OnDebugObjectChanged(UPCGComponent* InPCGComponent);
	void OnInspectedNodeChanged(UPCGNode* InPCGNode);

	void RebuildAttributeList(UPCGComponent* InPCGComponent);
	void RebuildAttributeList();

	TSharedRef<ITableRow> OnGenerateRow(PCGListviewItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const;

	/** Pointer back to the PCG editor that owns us */
	TWeakPtr<FPCGEditor> PCGEditorPtr;

	/** Cached PCGComponent being viewed */
	UPCGComponent* PCGComponent = nullptr;

	TSharedPtr<SHeaderRow> ListViewHeader;
	TSharedPtr<SListView<PCGListviewItemPtr>> ListView;
	TArray<PCGListviewItemPtr> ListViewItems;
};
