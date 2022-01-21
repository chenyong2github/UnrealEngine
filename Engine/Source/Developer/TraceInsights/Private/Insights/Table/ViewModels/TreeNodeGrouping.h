// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Delegates/DelegateCombinations.h"

// Insights
#include "Insights/Common/SimpleRtti.h"
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

#include <atomic>

namespace Insights
{

class FTable;

////////////////////////////////////////////////////////////////////////////////////////////////////

class ITreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI_BASE(ITreeNodeGrouping)

public:
	virtual FText GetShortName() const = 0;
	virtual FText GetTitleName() const = 0;
	virtual FText GetDescription() const = 0;

	virtual FName GetBrushName() const = 0;
	virtual const FSlateBrush* GetIcon() const = 0;

	virtual FName GetColumnId() const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTreeNodeGroupInfo
{
	FName Name;
	bool IsExpanded;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeGrouping : public ITreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGrouping, ITreeNodeGrouping)

public:
	FTreeNodeGrouping(const FText& InShortName, const FText& InTitleName, const FText& InDescription, const FName InBrushName, const FSlateBrush* InIcon);
	virtual ~FTreeNodeGrouping() {}

	virtual FText GetShortName() const override { return ShortName; }
	virtual FText GetTitleName() const override { return TitleName; }
	virtual FText GetDescription() const override { return Description; }

	virtual FName GetBrushName() const override { return BrushName; }
	virtual const FSlateBrush* GetIcon() const override { return Icon; }

	virtual FName GetColumnId() const override { return NAME_None; }

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const { return { FName(), false }; }
	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const;

protected:
	FText ShortName;
	FText TitleName;
	FText Description;
	FName BrushName;
	const FSlateBrush* Icon;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a single group for all nodes. */
class FTreeNodeGroupingFlat : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingFlat, FTreeNodeGrouping)

public:
	FTreeNodeGroupingFlat();
	virtual ~FTreeNodeGroupingFlat() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each unique value. */
class FTreeNodeGroupingByUniqueValue : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByUniqueValue, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeGroupingByUniqueValue() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;

	virtual FName GetColumnId() const override { return ColumnRef->GetId(); }
	TSharedRef<FTableColumn> GetColumn() const { return ColumnRef; }

private:
	TSharedRef<FTableColumn> ColumnRef;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each unique value (assumes data type of cell values is a simple type). */
template<typename Type>
class TTreeNodeGroupingByUniqueValue : public FTreeNodeGroupingByUniqueValue
{
public:
	TTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef) : FTreeNodeGroupingByUniqueValue(InColumnRef) {}
	virtual ~TTreeNodeGroupingByUniqueValue() {}

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const override;

private:
	static Type GetValue(const FTableCellValue& CellValue);
	static FText GetValueAsText(const FTableColumn& Column, const FTableTreeNode& Node)
	{
		return Column.GetValueAsTooltipText(Node);
	}
};

template<typename Type>
void TTreeNodeGroupingByUniqueValue<Type>::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const
{
	TMap<Type, FTableTreeNodePtr> GroupMap;
	FTableTreeNodePtr UnsetGroupPtr = nullptr;

	ParentGroup.ClearChildren();

	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (bCancelGrouping)
		{
			return;
		}

		if (NodePtr->IsGroup())
		{
			ParentGroup.AddChildAndSetGroupPtr(NodePtr);
			continue;
		}

		FTableTreeNodePtr GroupPtr = nullptr;

		const FTableColumn& Column = *GetColumn();
		const TOptional<FTableCellValue> CellValue = Column.GetValue(*NodePtr);
		if (CellValue.IsSet())
		{
			const Type Value = GetValue(CellValue.GetValue());

			FTableTreeNodePtr* GroupPtrPtr = GroupMap.Find(Value);
			if (!GroupPtrPtr)
			{
				FText ValueAsText = GetValueAsText(Column, *NodePtr);
				FStringView GroupName(ValueAsText.ToString());
				if (GroupName.Len() >= NAME_SIZE)
				{
					GroupName = FStringView(GroupName.GetData(), NAME_SIZE - 1);
				}
				GroupPtr = MakeShared<FTableTreeNode>(FName(GroupName, 0), InParentTable);
				GroupPtr->SetExpansion(false);
				ParentGroup.AddChildAndSetGroupPtr(GroupPtr);
				GroupMap.Add(Value, GroupPtr);
			}
			else
			{
				GroupPtr = *GroupPtrPtr;
			}
		}
		else
		{
			if (!UnsetGroupPtr)
			{
				UnsetGroupPtr = MakeShared<FTableTreeNode>(FName(TEXT("<unset>")), InParentTable);
				UnsetGroupPtr->SetExpansion(false);
				ParentGroup.AddChildAndSetGroupPtr(UnsetGroupPtr);
			}
			GroupPtr = UnsetGroupPtr;
		}

		GroupPtr->AddChildAndSetGroupPtr(NodePtr);
	}
}

typedef TTreeNodeGroupingByUniqueValue<bool> FTreeNodeGroupingByUniqueValueBool;
typedef TTreeNodeGroupingByUniqueValue<int64> FTreeNodeGroupingByUniqueValueInt64;
typedef TTreeNodeGroupingByUniqueValue<float> FTreeNodeGroupingByUniqueValueFloat;
typedef TTreeNodeGroupingByUniqueValue<double> FTreeNodeGroupingByUniqueValueDouble;
typedef TTreeNodeGroupingByUniqueValue<const TCHAR*> FTreeNodeGroupingByUniqueValueCString;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each first letter of node names. */
class FTreeNodeGroupingByNameFirstLetter : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByNameFirstLetter, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByNameFirstLetter();
	virtual ~FTreeNodeGroupingByNameFirstLetter() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each type. */
class FTreeNodeGroupingByType : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FTreeNodeGroupingByType, FTreeNodeGrouping);

public:
	FTreeNodeGroupingByType();
	virtual ~FTreeNodeGroupingByType() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
