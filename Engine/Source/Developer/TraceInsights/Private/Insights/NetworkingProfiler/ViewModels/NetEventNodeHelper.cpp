// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetEventNodeHelper.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "NetEventNode"

////////////////////////////////////////////////////////////////////////////////////////////////////
// NetEventNode Type Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText NetEventNodeTypeHelper::ToText(const ENetEventNodeType NodeType)
{
	static_assert(static_cast<int>(ENetEventNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case ENetEventNodeType::NetEvent:	return LOCTEXT("Type_Name_NetEvent", "Net Event");
		case ENetEventNodeType::Group:		return LOCTEXT("Type_Name_Group", "Group");
		default:							return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText NetEventNodeTypeHelper::ToDescription(const ENetEventNodeType NodeType)
{
	static_assert(static_cast<int>(ENetEventNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case ENetEventNodeType::NetEvent:	return LOCTEXT("Type_Desc_NetEvent", "Net event");
		case ENetEventNodeType::Group:		return LOCTEXT("Type_Desc_Group", "Group node");
		default:							return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName NetEventNodeTypeHelper::ToBrushName(const ENetEventNodeType NodeType)
{
	static_assert(static_cast<int>(ENetEventNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case ENetEventNodeType::NetEvent:	return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO: "Icons.NetEvent"
		case ENetEventNodeType::Group:		return TEXT("Profiler.Misc.GenericGroup"); //TODO: "Icons.GenericGroup"
		default:							return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* NetEventNodeTypeHelper::GetIconForGroup()
{
	return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* NetEventNodeTypeHelper::GetIconForNetEventNodeType(const ENetEventNodeType NodeType)
{
	static_assert(static_cast<int>(ENetEventNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case ENetEventNodeType::NetEvent:	return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.NetEvent"));
		case ENetEventNodeType::Group:		return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
		default:							return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// NetEventNode Grouping Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText NetEventNodeGroupingHelper::ToText(const ENetEventGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(ENetEventGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case ENetEventGroupingMode::Flat:		return LOCTEXT("Grouping_Name_Flat",	"Flat");
		case ENetEventGroupingMode::ByName:		return LOCTEXT("Grouping_Name_ByName",	"Name");
		case ENetEventGroupingMode::ByType:		return LOCTEXT("Grouping_Name_ByType",	"Event Type");
		case ENetEventGroupingMode::ByLevel:	return LOCTEXT("Grouping_Name_ByLevel", "Level");
		default:								return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText NetEventNodeGroupingHelper::ToDescription(const ENetEventGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(ENetEventGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case ENetEventGroupingMode::Flat:		return LOCTEXT("Grouping_Desc_Flat",	"Creates a single group. Includes all net events.");
		case ENetEventGroupingMode::ByName:		return LOCTEXT("Grouping_Desc_ByName",	"Creates one group for one letter.");
		case ENetEventGroupingMode::ByType:		return LOCTEXT("Grouping_Desc_ByType",	"Creates one group for each net event type.");
		case ENetEventGroupingMode::ByLevel:	return LOCTEXT("Grouping_Desc_ByLevel", "Creates one group for each level.");
		default:								return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName NetEventNodeGroupingHelper::ToBrushName(const ENetEventGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(ENetEventGroupingMode::InvalidOrMax) == 4, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case ENetEventGroupingMode::Flat:		return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO: "Icons.Grouping.Flat"
		case ENetEventGroupingMode::ByName:		return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO: "Icons.Grouping.ByName"
		case ENetEventGroupingMode::ByType:		return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO
		case ENetEventGroupingMode::ByLevel:	return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO
		default:								return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
