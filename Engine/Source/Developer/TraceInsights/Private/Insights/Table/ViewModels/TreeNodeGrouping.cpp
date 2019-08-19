// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TreeNodeGrouping.h"

#define LOCTEXT_NAMESPACE "Insights_TreeNode"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGrouping::FTreeNodeGrouping(FText InName, FText InDescription, FName InBrushName,  FSlateBrush* InIcon)
	: Name(InName)
	, Description(InDescription)
	, BrushName(InBrushName)
	, Icon(InIcon)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupingFlat::FTreeNodeGroupingFlat()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_Name_Flat", "Flat"),
		LOCTEXT("Grouping_Desc_Flat", "Creates a single group. Includes all items."),
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

FTreeNodeGroupingByNameFirstLetter::FTreeNodeGroupingByNameFirstLetter()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByName_Name", "Name"),
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
		LOCTEXT("Grouping_ByType_Name", "Type"),
		LOCTEXT("Grouping_ByType_Desc", "Creates a group for each type."),
		TEXT("Profiler.FiltersAndPresets.StatTypeIcon"), //TODO
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeGroupInfo FTreeNodeGroupingByType::GetGroupForNode(const FBaseTreeNodePtr InNode) const
{
	return { InNode->GetTypeId(), true };
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
