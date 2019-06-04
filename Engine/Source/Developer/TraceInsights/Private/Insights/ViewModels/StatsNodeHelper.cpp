// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StatsNodeHelper.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "StatsNode"

////////////////////////////////////////////////////////////////////////////////////////////////////
// StatsNode Type Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText StatsNodeTypeHelper::ToName(const EStatsNodeType NodeType)
{
	static_assert(static_cast<int>(EStatsNodeType::InvalidOrMax) == 3, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EStatsNodeType::Float:		return LOCTEXT("Stats_Name_Float", "Float");
		case EStatsNodeType::Int64:		return LOCTEXT("Stats_Name_Int64", "Integer");
		case EStatsNodeType::Group:		return LOCTEXT("Stats_Name_Group", "Group");
		default:						return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText StatsNodeTypeHelper::ToDescription(const EStatsNodeType NodeType)
{
	static_assert(static_cast<int>(EStatsNodeType::InvalidOrMax) == 3, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EStatsNodeType::Float:		return LOCTEXT("Stats_Desc_Float", "Float number stats");
		case EStatsNodeType::Int64:		return LOCTEXT("Stats_Desc_Int64", "Integer number stats");
		case EStatsNodeType::Group:		return LOCTEXT("Stats_Desc_Group", "Group stats node");
		default:						return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName StatsNodeTypeHelper::ToBrushName(const EStatsNodeType NodeType)
{
	static_assert(static_cast<int>(EStatsNodeType::InvalidOrMax) == 3, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EStatsNodeType::Float:		return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO: "Icons.StatsType.Float"
		case EStatsNodeType::Int64:		return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO: "Icons.StatsType.Int64"
		case EStatsNodeType::Group:		return TEXT("Profiler.Misc.GenericGroup"); //TODO: "Icons.GenericGroup"
		default:						return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* StatsNodeTypeHelper::GetIconForGroup()
{
	return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* StatsNodeTypeHelper::GetIconForStatsNodeType(const EStatsNodeType NodeType)
{
	static_assert(static_cast<int>(EStatsNodeType::InvalidOrMax) == 3, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EStatsNodeType::Float:		return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.StatsType.Float"));
		case EStatsNodeType::Int64:		return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberInt")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.StatsType.Int64"));
		case EStatsNodeType::Group:		return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
		default:						return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// StatsNode Grouping Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText StatsNodeGroupingHelper::ToName(const EStatsGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EStatsGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case EStatsGroupingMode::Flat:				return LOCTEXT("Grouping_Name_Flat",			"Flat");
		case EStatsGroupingMode::ByName:			return LOCTEXT("Grouping_Name_ByName",			"Stats Name");
		case EStatsGroupingMode::ByMetaGroupName:	return LOCTEXT("Grouping_Name_MetaGroupName",	"Meta Group Name");
		case EStatsGroupingMode::ByType:			return LOCTEXT("Grouping_Name_Type",			"Stats Type");
		default:									return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText StatsNodeGroupingHelper::ToDescription(const EStatsGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EStatsGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case EStatsGroupingMode::Flat:				return LOCTEXT("Grouping_Desc_Flat",			"Creates a single group. Includes all stats.");
		case EStatsGroupingMode::ByName:			return LOCTEXT("Grouping_Desc_ByName",			"Creates one group for one letter.");
		case EStatsGroupingMode::ByMetaGroupName:	return LOCTEXT("Grouping_Desc_MetaGroupName",	"Creates groups based on metadata group names of stats.");
		case EStatsGroupingMode::ByType:			return LOCTEXT("Grouping_Desc_Type",			"Creates one group for each stats type.");
		default:									return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName StatsNodeGroupingHelper::ToBrushName(const EStatsGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EStatsGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		//TODO: "UnrealProfiler.*Icon"
		case EStatsGroupingMode::Flat:				return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO: "Icons.Grouping.Flat"
		case EStatsGroupingMode::ByName:			return TEXT("Profiler.FiltersAndPresets.Group1NameIcon"); //TODO: "Icons.Grouping.ByName"
		case EStatsGroupingMode::ByMetaGroupName:	return TEXT("Profiler.FiltersAndPresets.StatNameIcon"); //TODO
		case EStatsGroupingMode::ByType:			return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO
		default:									return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
