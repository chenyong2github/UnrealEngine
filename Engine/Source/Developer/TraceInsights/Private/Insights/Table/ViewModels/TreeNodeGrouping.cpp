// Copyright Epic Games, Inc. All Rights Reserved.

#include "TreeNodeGrouping.h"

#include "Insights/Table/ViewModels/TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights_TreeNode"

namespace Insights
{

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

FTreeNodeGroupingFlat::FTreeNodeGroupingFlat()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_Flat_ShortName", "All"),
		LOCTEXT("Grouping_Flat_TitleName", "Flat (All)"),
		LOCTEXT("Grouping_Flat_Desc", "Creates a single group. Includes all items."),
		TEXT("Profiler.FiltersAndPresets.GroupNameIcon"), //TODO: "Icons.Grouping.Flat"
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingFlat::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	return { FName(TEXT("All")), true };
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByUniqueValue::FTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef)
	: FTreeNodeGrouping(
		InColumnRef->GetTitleName(),
		FText::Format(LOCTEXT("Grouping_ByUniqueValue_TitleNameFmt", "Unique Values - {0}"), InColumnRef->GetTitleName()),
		LOCTEXT("Grouping_ByUniqueValue_Desc", "Creates a group for each unique value."),
		TEXT("Profiler.FiltersAndPresets.Group1NameIcon"), //TODO: "Icons.Grouping.ByName"
		nullptr)
	, ColumnRef(InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByUniqueValue::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	FTableTreeNodePtr TableTreeNodePtr = StaticCastSharedPtr<FTableTreeNode>(InNode);
	return { FName(*ColumnRef->GetValueAsText(*TableTreeNodePtr).ToString()), false };
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByNameFirstLetter::FTreeNodeGroupingByNameFirstLetter()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByName_ShortName", "Name"),
		LOCTEXT("Grouping_ByName_TitleName", "Name (First Letter)"),
		LOCTEXT("Grouping_ByName_Desc", "Creates a group for each first letter of node names."),
		TEXT("Profiler.FiltersAndPresets.Group1NameIcon"), //TODO: "Icons.Grouping.ByName"
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByNameFirstLetter::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	return { *InNode->GetName().GetPlainNameString().Left(1), false };
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingByType::FTreeNodeGroupingByType()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByTypeName_ShortName", "TypeName"),
		LOCTEXT("Grouping_ByTypeName_TitleName", "TypeName"),
		LOCTEXT("Grouping_ByTypeName_Desc", "Creates a group for each node type."),
		TEXT("Profiler.FiltersAndPresets.StatTypeIcon"), //TODO
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
