// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimerNodeHelper.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "TimerNode"

////////////////////////////////////////////////////////////////////////////////////////////////////
// TimerNode Type Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText TimerNodeTypeHelper::ToName(const ETimerNodeType NodeType)
{
	static_assert(ETimerNodeType::InvalidOrMax == 4, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
	case ETimerNodeType::GpuScope:			return LOCTEXT("Timer_Name_GpuScope", "GPU");
	case ETimerNodeType::ComputeScope:		return LOCTEXT("Timer_Name_ComputeScope", "Compute");
	case ETimerNodeType::CpuScope:			return LOCTEXT("Timer_Name_CpuScope", "CPU");
	case ETimerNodeType::Group:				return LOCTEXT("Timer_Name_Group", "Group"); //TODO
	default: return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText TimerNodeTypeHelper::ToDescription(const ETimerNodeType NodeType)
{
	static_assert(ETimerNodeType::InvalidOrMax == 4, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
	case ETimerNodeType::GpuScope:			return LOCTEXT("Timer_Desc_GpuScope", "GPU scope timer");
	case ETimerNodeType::ComputeScope:		return LOCTEXT("Timer_Desc_ComputeScope", "Compute scope timer");
	case ETimerNodeType::CpuScope:			return LOCTEXT("Timer_Desc_CpuScope", "CPU scope timer");
	case ETimerNodeType::Group:				return LOCTEXT("Timer_Desc_Group", "Group timer node"); //TODO
	default: return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName TimerNodeTypeHelper::ToBrushName(const ETimerNodeType NodeType)
{
	static_assert(ETimerNodeType::InvalidOrMax == 4, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
	case ETimerNodeType::GpuScope:			return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //im:TODO
	case ETimerNodeType::ComputeScope:		return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //im:TODO
	case ETimerNodeType::CpuScope:			return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //im:TODO
	case ETimerNodeType::Group:				return TEXT("Profiler.Misc.GenericGroup"); //TODO
	default: return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* TimerNodeTypeHelper::GetIconForGroup()
{
	return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* TimerNodeTypeHelper::GetIconForTimerNodeType(const ETimerNodeType NodeType)
{
	static_assert(ETimerNodeType::InvalidOrMax == 4, "Not all cases are handled in switch below!?");
	switch (NodeType)
	{
	case ETimerNodeType::GpuScope:			return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO
	case ETimerNodeType::ComputeScope:		return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO
	case ETimerNodeType::CpuScope:			return FEditorStyle::GetBrush(TEXT("Profiler.Type.NumberFloat")); //TODO
	case ETimerNodeType::Group:				return FEditorStyle::GetBrush(TEXT("Profiler.Misc.GenericGroup")); //TODO
	default: return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// TimerNode Grouping Helper
////////////////////////////////////////////////////////////////////////////////////////////////////

FText TimerNodeGroupingHelper::ToName(const ETimerGroupingMode GroupingMode)
{
	switch (GroupingMode)
	{
	case ETimerGroupingMode::Flat:					return LOCTEXT("Grouping_Name_Flat",				"Flat");
	case ETimerGroupingMode::ByName:				return LOCTEXT("Grouping_Name_ByName",				"Timer Name");
	case ETimerGroupingMode::ByMetaGroupName:		return LOCTEXT("Grouping_Name_MetaGroupName",		"Meta Group Name");
	case ETimerGroupingMode::ByType:				return LOCTEXT("Grouping_Name_Type",				"Timer Type");
	case ETimerGroupingMode::ByTotalInclusiveTime:	return LOCTEXT("Grouping_Name_TotalInclusiveTime",	"Total Inclusive Time");
	case ETimerGroupingMode::ByTotalExclusiveTime:	return LOCTEXT("Grouping_Name_TotalExclusiveTime",	"Total Exclusive Time");
	case ETimerGroupingMode::ByInstanceCount:		return LOCTEXT("Grouping_Name_InstanceCount",		"Instance Count");

	default: return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText TimerNodeGroupingHelper::ToDescription(const ETimerGroupingMode GroupingMode)
{
	switch (GroupingMode)
	{
	case ETimerGroupingMode::Flat:					return LOCTEXT("Grouping_Desc_Flat",				"Creates a single group. Includes all timers.");
	case ETimerGroupingMode::ByName:				return LOCTEXT("Grouping_Desc_ByName",				"Creates one group for one letter.");
	case ETimerGroupingMode::ByMetaGroupName:		return LOCTEXT("Grouping_Desc_MetaGroupName",		"Creates groups based on timer metadata group names.");
	case ETimerGroupingMode::ByType:				return LOCTEXT("Grouping_Desc_Type",				"Creates one group for each timer type.");
	case ETimerGroupingMode::ByTotalInclusiveTime:	return LOCTEXT("Grouping_Desc_TotalInclusiveTime",	"Creates one group for each logarithmic range ie. 0.001 - 0.01, 0.01 - 0.1, 0.1 - 1.0, 1.0 - 10.0 etc");
	case ETimerGroupingMode::ByTotalExclusiveTime:	return LOCTEXT("Grouping_Desc_TotalExclusiveTime",	"Creates one group for each logarithmic range ie. 0.001 - 0.01, 0.01 - 0.1, 0.1 - 1.0, 1.0 - 10.0 etc");
	case ETimerGroupingMode::ByInstanceCount:		return LOCTEXT("Grouping_Desc_InstanceCount",		"Creates one group for each logarithmic range ie. 0, 1 - 10, 10 - 100, 100 - 1000, etc");

	default: return LOCTEXT("InvalidOrMax", "InvalidOrMax");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName TimerNodeGroupingHelper::ToBrushName(const ETimerGroupingMode GroupingMode)
{
	switch (GroupingMode)
	{
	//TODO: "UnrealProfiler.*Icon"
	case ETimerGroupingMode::Flat:					return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO
	case ETimerGroupingMode::ByName:				return TEXT("Profiler.FiltersAndPresets.GroupNameIcon"); //TODO
	case ETimerGroupingMode::ByMetaGroupName	:	return TEXT("Profiler.FiltersAndPresets.StatNameIcon"); //TODO
	case ETimerGroupingMode::ByType:				return TEXT("Profiler.FiltersAndPresets.StatTypeIcon"); //TODO
	case ETimerGroupingMode::ByTotalInclusiveTime:	return TEXT("Profiler.FiltersAndPresets.StatValueIcon"); //TODO
	case ETimerGroupingMode::ByTotalExclusiveTime:	return TEXT("Profiler.FiltersAndPresets.StatValueIcon"); //TODO
	case ETimerGroupingMode::ByInstanceCount:		return TEXT("Profiler.FiltersAndPresets.StatValueIcon"); //TODO

	default: return NAME_None;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
