// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/SCurveEditorTree.h"
#include "Tree/CurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"

#include "CurveEditor.h"

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SExpanderArrow.h"

template <> struct TListTypeTraits<FCurveEditorTreeItemID>
{
public:
	struct NullableType : FCurveEditorTreeItemID
	{
		NullableType(TYPE_OF_NULLPTR){}
		NullableType(FCurveEditorTreeItemID Other) : FCurveEditorTreeItemID(Other) {}
	};

	using MapKeyFuncs = TDefaultMapHashableKeyFuncs<FCurveEditorTreeItemID, TSharedRef<ITableRow>, false>;
	using MapKeyFuncsSparse = TDefaultMapHashableKeyFuncs<FCurveEditorTreeItemID, FSparseItemInfo, false>;
	using SetKeyFuncs = DefaultKeyFuncs<FCurveEditorTreeItemID>;

	template<typename U>
	static void AddReferencedObjects(FReferenceCollector&, TArray<FCurveEditorTreeItemID>&, TSet<FCurveEditorTreeItemID>&, TMap< const U*, FCurveEditorTreeItemID >&) {}

	static bool IsPtrValid(NullableType InPtr)
	{
		return InPtr.IsValid();
	}

	static void ResetPtr(NullableType& InPtr)
	{
		InPtr = nullptr;
	}

	static NullableType MakeNullPtr()
	{
		return nullptr;
	}

	static FCurveEditorTreeItemID NullableItemTypeConvertToItemType(NullableType InPtr)
	{
		return InPtr;
	}

	static FString DebugDump(FCurveEditorTreeItemID InPtr)
	{
		return FString::Printf(TEXT("%d"), InPtr.GetValue());
	}

	class SerializerType{};
};
template <>
struct TIsValidListItem<FCurveEditorTreeItemID>
{
	enum
	{
		Value = true
	};
};

class SCurveEditorTreeView : public STreeView<FCurveEditorTreeItemID>
{};

struct SCurveEditorTableRow : SMultiColumnTableRow<FCurveEditorTreeItemID>
{
	FCurveEditorTreeItemID TreeItemID;
	TWeakPtr<FCurveEditor> WeakCurveEditor;

	void Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID)
	{
		TreeItemID = InTreeItemID;
		WeakCurveEditor = InCurveEditor;

		SMultiColumnTableRow::Construct(InArgs, OwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
		TSharedPtr<ICurveEditorTreeItem> TreeItem = CurveEditor ? CurveEditor->GetTreeItem(TreeItemID).GetItem() : nullptr;

		TSharedPtr<SWidget> Widget;
		if (TreeItem.IsValid())
		{
			Widget = TreeItem->GenerateCurveEditorTreeWidget(InColumnName, WeakCurveEditor, TreeItemID);
		}

		if (!Widget.IsValid())
		{
			Widget = SNullWidget::NullWidget;
		}

		if (InColumnName == ICurveEditorTreeItem::ColumnNames.Label)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.Padding(FMargin(3.f, 0.f, 0.f, 0.f))
				.VAlign(VAlign_Center)
				[
					Widget.ToSharedRef()
				];
		}

		return Widget.ToSharedRef();
	}
};

void SCurveEditorTree::Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> InCurveEditor)
{
	HeaderRow = SNew(SHeaderRow)
		.Visibility(EVisibility::Collapsed)

		+ SHeaderRow::Column(ICurveEditorTreeItem::ColumnNames.Label)

		+ SHeaderRow::Column(ICurveEditorTreeItem::ColumnNames.PinHeader)
		.FixedWidth(24.f);

	CurveEditor = InCurveEditor;

	ChildSlot
	[
		SAssignNew(TreeView, SCurveEditorTreeView)
		.SelectionMode(ESelectionMode::Multi)
		.HeaderRow(HeaderRow)
		.HighlightParentNodesForSelection(true)
		.TreeItemsSource(&InCurveEditor->GetRootTreeItems())
		.OnGetChildren(this, &SCurveEditorTree::GetTreeItemChildren)
		.OnGenerateRow(this, &SCurveEditorTree::GenerateRow)
		.OnSetExpansionRecursive(this, &SCurveEditorTree::SetItemExpansionRecursive)
		.OnSelectionChanged_Lambda(
			[this](TListTypeTraits<FCurveEditorTreeItemID>::NullableType InItemID, ESelectInfo::Type Type)
			{
				this->OnTreeSelectionChanged(InItemID, Type);
			}
		)
	];

	CurveEditor->OnTreeChanged().AddSP(TreeView.ToSharedRef(), &SCurveEditorTreeView::RequestTreeRefresh);
}

FReply SCurveEditorTree::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		TreeView->ClearSelection();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<ITableRow> SCurveEditorTree::GenerateRow(FCurveEditorTreeItemID ItemID, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCurveEditorTableRow, OwnerTable, CurveEditor, ItemID);
}

void SCurveEditorTree::GetTreeItemChildren(FCurveEditorTreeItemID Parent, TArray<FCurveEditorTreeItemID>& OutChildren)
{
	TArrayView<const FCurveEditorTreeItemID> Children = CurveEditor->GetTreeItem(Parent).GetChildren();
	OutChildren.Append(Children.GetData(), Children.Num());
}

void SCurveEditorTree::OnTreeSelectionChanged(FCurveEditorTreeItemID, ESelectInfo::Type)
{
	CurveEditor->SetDirectTreeSelection(TreeView->GetSelectedItems());
}

void SCurveEditorTree::SetItemExpansionRecursive(FCurveEditorTreeItemID Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		TreeView->SetItemExpansion(Model, bInExpansionState);

		TArray<FCurveEditorTreeItemID> Children;
		GetTreeItemChildren(Model, Children);

		for (FCurveEditorTreeItemID Child : Children)
		{
			if (Child.IsValid())
			{
				SetItemExpansionRecursive(Child, bInExpansionState);
			}
		}
	}
}