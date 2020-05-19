// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemTagNodeHelper.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "MemTagNode"

////////////////////////////////////////////////////////////////////////////////////////////////////
// MemTagNode Type Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText MemTagNodeTypeHelper::ToText(const EMemTagNodeType NodeType)
{
	static_assert(static_cast<int>(EMemTagNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EMemTagNodeType::MemTag:	return LOCTEXT("Type_Name_MemTag", "Memory Tag");
		case EMemTagNodeType::Group:	return LOCTEXT("Type_Name_Group", "Group");
		default:						return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText MemTagNodeTypeHelper::ToDescription(const EMemTagNodeType NodeType)
{
	static_assert(static_cast<int>(EMemTagNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EMemTagNodeType::MemTag:	return LOCTEXT("Type_Desc_MemTag", "Low level memory tag");
		case EMemTagNodeType::Group:	return LOCTEXT("Type_Desc_Group", "Group node");
		default:						return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName MemTagNodeTypeHelper::ToBrushName(const EMemTagNodeType NodeType)
{
	static_assert(static_cast<int>(EMemTagNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EMemTagNodeType::MemTag:	return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO: "Icons.MemTag"
		case EMemTagNodeType::Group:	return TEXT("Profiler.Misc.GenericGroup"); //TODO: "Icons.GenericGroup"
		default:						return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* MemTagNodeTypeHelper::GetIconForGroup()
{
	return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* MemTagNodeTypeHelper::GetIconForMemTagNodeType(const EMemTagNodeType NodeType)
{
	static_assert(static_cast<int>(EMemTagNodeType::InvalidOrMax) == 2, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
		case EMemTagNodeType::MemTag:	return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.MemTag"));
		case EMemTagNodeType::Group:	return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO: FInsightsStyle::GetBrush(TEXT("Icons.GenericGroup"));
		default:						return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// MemTagNode Grouping Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText MemTagNodeGroupingHelper::ToText(const EMemTagNodeGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EMemTagNodeGroupingMode::InvalidOrMax) == 5, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case EMemTagNodeGroupingMode::Flat:			return LOCTEXT("Grouping_Name_Flat",		"Flat");
		case EMemTagNodeGroupingMode::ByName:		return LOCTEXT("Grouping_Name_ByName",		"Name");
		case EMemTagNodeGroupingMode::ByType:		return LOCTEXT("Grouping_Name_ByType",		"Event Type");
		case EMemTagNodeGroupingMode::ByTracker:	return LOCTEXT("Grouping_Name_ByTracker",	"Tracker");
		case EMemTagNodeGroupingMode::ByParent:		return LOCTEXT("Grouping_Name_ByParent",	"Hierarchy");
		default:									return LOCTEXT("InvalidOrMax",				"InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText MemTagNodeGroupingHelper::ToDescription(const EMemTagNodeGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EMemTagNodeGroupingMode::InvalidOrMax) == 5, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case EMemTagNodeGroupingMode::Flat:			return LOCTEXT("Grouping_Desc_Flat",		"Creates a single group. Includes all LLM tags.");
		case EMemTagNodeGroupingMode::ByName:		return LOCTEXT("Grouping_Desc_ByName",		"Creates one group for one letter.");
		case EMemTagNodeGroupingMode::ByType:		return LOCTEXT("Grouping_Desc_ByType",		"Creates one group for each LLM tag type.");
		case EMemTagNodeGroupingMode::ByTracker:	return LOCTEXT("Grouping_Desc_ByTracker",	"Groups LLM tags by tracker.");
		case EMemTagNodeGroupingMode::ByParent:		return LOCTEXT("Grouping_Desc_ByParent",	"Groups LLM tags by their hierarchy.");
		default:									return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName MemTagNodeGroupingHelper::ToBrushName(const EMemTagNodeGroupingMode GroupingMode)
{
	static_assert(static_cast<int>(EMemTagNodeGroupingMode::InvalidOrMax) == 5, "Not all cases are handled in switch below!?");
	switch (GroupingMode)
	{
		case EMemTagNodeGroupingMode::Flat:			return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO: "Icons.Grouping.Flat"
		case EMemTagNodeGroupingMode::ByName:		return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO: "Icons.Grouping.ByName"
		case EMemTagNodeGroupingMode::ByType:		return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO
		case EMemTagNodeGroupingMode::ByTracker:	return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO
		case EMemTagNodeGroupingMode::ByParent:		return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO
		default:									return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
