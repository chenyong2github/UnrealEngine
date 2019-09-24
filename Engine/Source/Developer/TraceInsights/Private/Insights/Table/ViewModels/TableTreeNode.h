// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/Table.h" // for FTableRowId
#include "Insights/Table/ViewModels/TableCellValue.h"

namespace Insights
{

//class FTable;
//class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTableTreeNode;

/** Type definition for shared pointers to instances of FTableTreeNode. */
typedef TSharedPtr<class FTableTreeNode> FTableTreeNodePtr;

/** Type definition for shared references to instances of FTableTreeNode. */
typedef TSharedRef<class FTableTreeNode> FTableTreeNodeRef;

/** Type definition for shared references to const instances of FTableTreeNode. */
typedef TSharedRef<const class FTableTreeNode> FTableTreeNodeRefConst;

/** Type definition for weak references to instances of FTableTreeNode. */
typedef TWeakPtr<class FTableTreeNode> FTableTreeNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Table Tree Node View Model.
 * Class used to store information about a generic table tree node (used in STableTreeView).
 */
class FTableTreeNode : public FBaseTreeNode
{
public:
	static const FName TypeName;

public:
	/** Initialization constructor for a table record node. */
	FTableTreeNode(uint64 InId, const FName InName, TWeakPtr<FTable> InParentTable, int32 InRowIndex)
		: FBaseTreeNode(InId, InName, false)
		, ParentTable(InParentTable)
		, RowId(InRowIndex)
	{
	}

	/** Initialization constructor for a group node. */
	FTableTreeNode(const FName InGroupName, TWeakPtr<FTable> InParentTable)
		: FBaseTreeNode(0, InGroupName, true)
		, ParentTable(InParentTable)
		, RowId(FTableRowId::InvalidRowIndex)
	{
	}

	virtual const FName& GetTypeName() const override { return TypeName; }

	TWeakPtr<FTable> GetParentTable() { return ParentTable; }
	FTableRowId GetRowId() const { return RowId; }
	int32 GetRowIndex() const { return RowId.RowIndex; }

	void ResetAggregatedValues() { AggregatedValues.Reset(); }
	bool HasAggregatedValue(const FName& ColumnId) const { return AggregatedValues.Contains(ColumnId); }
	const FTableCellValue* FindAggregatedValue(const FName& ColumnId) const { return AggregatedValues.Find(ColumnId); }
	const FTableCellValue& GetAggregatedValue(const FName& ColumnId) const { return AggregatedValues.FindChecked(ColumnId); }
	void AddAggregatedValue(const FName& ColumnId, const FTableCellValue& Value) { AggregatedValues.Add(ColumnId, Value); }
	void SetAggregatedValue(const FName& ColumnId, const FTableCellValue& Value) { AggregatedValues[ColumnId] = Value; }

protected:
	TWeakPtr<FTable> ParentTable;
	FTableRowId RowId;

	TMap<FName, FTableCellValue> AggregatedValues;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
