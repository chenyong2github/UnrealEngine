// Copyright Epic Games, Inc. All Rights Reserved.

#include "TreeNodeGrouping.h"

#include "Insights/Table/ViewModels/TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights_TreeNode"

namespace Insights
{

INSIGHTS_IMPLEMENT_RTTI(ITreeNodeGrouping)
INSIGHTS_IMPLEMENT_RTTI(FTreeNodeGrouping)
INSIGHTS_IMPLEMENT_RTTI(FTreeNodeGroupingFlat)
INSIGHTS_IMPLEMENT_RTTI(FTreeNodeGroupingByUniqueValue)
INSIGHTS_IMPLEMENT_RTTI(FTreeNodeGroupingByNameFirstLetter)
INSIGHTS_IMPLEMENT_RTTI(FTreeNodeGroupingByType)

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeGrouping
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGrouping::FTreeNodeGrouping(const FText& InShortName, const FText& InTitleName, const FText& InDescription, const FName InBrushName, const FSlateBrush* InIcon)
	: ShortName(InShortName)
	, TitleName(InTitleName)
	, Description(InDescription)
	, BrushName(InBrushName)
	, Icon(InIcon)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTreeNodeGrouping::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const
{
	TMap<FName, FTableTreeNodePtr> GroupMap;

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

		FTreeNodeGroupInfo GroupInfo = GetGroupForNode(NodePtr);
		FTableTreeNodePtr* GroupPtrPtr = GroupMap.Find(GroupInfo.Name);
		if (!GroupPtrPtr)
		{
			GroupPtr = MakeShared<FTableTreeNode>(GroupInfo.Name, InParentTable);
			GroupPtr->SetExpansion(GroupInfo.IsExpanded);
			ParentGroup.AddChildAndSetGroupPtr(GroupPtr);
			GroupMap.Add(GroupInfo.Name, GroupPtr);
		}
		else
		{
			GroupPtr = *GroupPtrPtr;
		}

		GroupPtr->AddChildAndSetGroupPtr(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeGroupingFlat
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingFlat::FTreeNodeGroupingFlat()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_Flat_ShortName", "All"),
		LOCTEXT("Grouping_Flat_TitleName", "Flat (All)"),
		LOCTEXT("Grouping_Flat_Desc", "Creates a single group. Includes all items."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTreeNodeGroupingFlat::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const
{
	ParentGroup.ClearChildren(1);

	FTableTreeNodePtr GroupPtr = MakeShared<FTableTreeNode>(FName(TEXT("All")), InParentTable);
	GroupPtr->SetExpansion(true);
	ParentGroup.AddChildAndSetGroupPtr(GroupPtr);

	GroupPtr->ClearChildren(Nodes.Num());
	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (bCancelGrouping)
		{
			return;
		}

		GroupPtr->AddChildAndSetGroupPtr(NodePtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeGroupingByUniqueValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByUniqueValue::FTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef)
	: FTreeNodeGrouping(
		InColumnRef->GetTitleName(),
		FText::Format(LOCTEXT("Grouping_ByUniqueValue_TitleNameFmt", "Unique Values - {0}"), InColumnRef->GetTitleName()),
		LOCTEXT("Grouping_ByUniqueValue_Desc", "Creates a group for each unique value."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
	, ColumnRef(InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByUniqueValue::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	FTableTreeNodePtr TableTreeNodePtr = StaticCastSharedPtr<FTableTreeNode>(InNode);
	FText ValueAsText = ColumnRef->GetValueAsText(*TableTreeNodePtr);
	FStringView GroupName(ValueAsText.ToString());
	if (GroupName.Len() >= NAME_SIZE)
	{
		GroupName = FStringView(GroupName.GetData(), NAME_SIZE - 1);
	}
	return { FName(GroupName, 0), false };
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TTreeNodeGroupingByUniqueValue specializations
////////////////////////////////////////////////////////////////////////////////////////////////////

template<> bool TTreeNodeGroupingByUniqueValue<bool>::GetValue(const FTableCellValue& CellValue) { return CellValue.Bool; }
template<> int64 TTreeNodeGroupingByUniqueValue<int64>::GetValue(const FTableCellValue& CellValue) { return CellValue.Int64; }
template<> float TTreeNodeGroupingByUniqueValue<float>::GetValue(const FTableCellValue& CellValue) { return CellValue.Float; }
template<> double TTreeNodeGroupingByUniqueValue<double>::GetValue(const FTableCellValue& CellValue) { return CellValue.Double; }
template<> const TCHAR* TTreeNodeGroupingByUniqueValue<const TCHAR*>::GetValue(const FTableCellValue& CellValue) { return CellValue.CString; }

template<>
FText TTreeNodeGroupingByUniqueValue<const TCHAR*>::GetValueAsText(const FTableColumn& Column, const FTableTreeNode& Node)
{
	return Column.GetValueAsText(Node);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeGroupingByNameFirstLetter
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByNameFirstLetter::FTreeNodeGroupingByNameFirstLetter()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByName_ShortName", "Name"),
		LOCTEXT("Grouping_ByName_TitleName", "Name (First Letter)"),
		LOCTEXT("Grouping_ByName_Desc", "Creates a group for each first letter of node names."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByNameFirstLetter::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	return { *InNode->GetName().GetPlainNameString().Left(1), false };
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeGroupingByType
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByType::FTreeNodeGroupingByType()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByTypeName_ShortName", "TypeName"),
		LOCTEXT("Grouping_ByTypeName_TitleName", "TypeName"),
		LOCTEXT("Grouping_ByTypeName_Desc", "Creates a group for each node type."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByType::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	return { InNode->GetTypeName(), true };
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
