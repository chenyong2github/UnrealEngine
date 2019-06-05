// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimersViewColumnFactory.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimerNodeHelper.h"

#define LOCTEXT_NAMESPACE "STimerView"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FTimersViewColumns::NameColumnID(TEXT("Name"));
const FName FTimersViewColumns::MetaGroupNameColumnID(TEXT("MetaGroupName"));
const FName FTimersViewColumns::TypeColumnID(TEXT("Type"));
const FName FTimersViewColumns::InstanceCountColumnID(TEXT("Count"));

// Inclusive Time columns
const FName FTimersViewColumns::TotalInclusiveTimeColumnID(TEXT("TotalInclTime"));
const FName FTimersViewColumns::MaxInclusiveTimeColumnID(TEXT("MaxInclTime"));
const FName FTimersViewColumns::UpperQuartileInclusiveTimeColumnID(TEXT("UpperQuartileInclTime"));
const FName FTimersViewColumns::AverageInclusiveTimeColumnID(TEXT("AverageInclTime"));
const FName FTimersViewColumns::MedianInclusiveTimeColumnID(TEXT("MedianInclTime"));
const FName FTimersViewColumns::LowerQuartileInclusiveTimeColumnID(TEXT("LowerQuartileInclTime"));
const FName FTimersViewColumns::MinInclusiveTimeColumnID(TEXT("MinInclTime"));

// Exclusive Time columns
const FName FTimersViewColumns::TotalExclusiveTimeColumnID(TEXT("TotalExclTime"));
const FName FTimersViewColumns::MaxExclusiveTimeColumnID(TEXT("MaxExclTime"));
const FName FTimersViewColumns::UpperQuartileExclusiveTimeColumnID(TEXT("UpperQuartileExclTime"));
const FName FTimersViewColumns::AverageExclusiveTimeColumnID(TEXT("AverageExclTime"));
const FName FTimersViewColumns::MedianExclusiveTimeColumnID(TEXT("MedianExclTime"));
const FName FTimersViewColumns::LowerQuartileExclusiveTimeColumnID(TEXT("LowerQuartileExclTime"));
const FName FTimersViewColumns::MinExclusiveTimeColumnID(TEXT("MinExclTime"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimersViewColumnFactory::FTimersViewColumnFactory()
{
	uint32 Index = 0;

	// FTimerViewColumn
	// (
	//     Order,
	//     Id,
	//     SearchId,
	//     ShortName,
	//     TitleName,
	//     Description,
	//     bIsVisible,
	//     Flags,
	//     HorizontalAlignment,
	//     InitialWidth, MinWidth, MaxWidth,
	//     GetFormattedValueFn
	// )

	//////////////////////////////////////////////////
	// Name

	const ETimersViewColumnFlags NameColumnFlags = ETimersViewColumnFlags::CanBeSorted |
												   ETimersViewColumnFlags::CanBeFiltered;

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::NameColumnID,
		TEXT("name"),
		LOCTEXT("TimerNameColumnName", "Name"),
		LOCTEXT("TimerNameColumnTitle", "Timer or Group Name"),
		LOCTEXT("TimerNameColumnDesc", "Name of the timer or group"),
		true,
		NameColumnFlags,
		HAlign_Left,
		246.0f, 10.0f, FLT_MAX,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromName(TimerNode.GetName()); }
	));

	//////////////////////////////////////////////////
	// Meta Group Name

	const ETimersViewColumnFlags MetaGroupNameColumnFlags = ETimersViewColumnFlags::CanBeHidden |
															ETimersViewColumnFlags::CanBeSorted |
															ETimersViewColumnFlags::CanBeFiltered;

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MetaGroupNameColumnID,
		TEXT("metagroupname"),
		LOCTEXT("TimerMetaGroupNameColumnName", "Meta Group"),
		LOCTEXT("TimerMetaGroupNameColumnTitle", "Meta Group Name"),
		LOCTEXT("TimerMetaGroupNameColumnDesc", "Name of the meta group"),
		false,
		MetaGroupNameColumnFlags,
		HAlign_Left,
		100.0f, 10.0f, FLT_MAX,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromName(TimerNode.GetMetaGroupName()); }
	));

	//////////////////////////////////////////////////
	// Type

	const ETimersViewColumnFlags TypeColumnFlags = ETimersViewColumnFlags::CanBeHidden |
												   ETimersViewColumnFlags::CanBeSorted |
												   ETimersViewColumnFlags::CanBeFiltered;

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::TypeColumnID,
		TEXT("type"),
		LOCTEXT("TimerTypeColumnName", "Type"),
		LOCTEXT("TimerTypeColumnTitle", "Type"),
		LOCTEXT("TimerTypeColumnDesc", "Type of timer or group"),
		false,
		TypeColumnFlags,
		HAlign_Left,
		60.0f, 10.0f, FLT_MAX,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return TimerNodeTypeHelper::ToName(TimerNode.GetType()); }
	));
		
	//////////////////////////////////////////////////

	const ETimersViewColumnFlags AggregatedStatsColumnFlags = ETimersViewColumnFlags::CanBeHidden |
															  ETimersViewColumnFlags::CanBeSorted |
															  ETimersViewColumnFlags::CanBeFiltered;

	//////////////////////////////////////////////////
	// Instance Count

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::InstanceCountColumnID,
		TEXT("count"),
		LOCTEXT("InstanceCountName", "Count"),
		LOCTEXT("InstanceCountTitle", "Instance Count"),
		LOCTEXT("InstanceCountDesc", "Number of timer's instances"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		60.0f, 0.0f, FLT_MAX,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::AsNumber(TimerNode.GetAggregatedStats().InstanceCount); }
	));

	//////////////////////////////////////////////////

	constexpr float TotalTimeColumnInitialWidth = 60.0f;
	constexpr float TotalTimeColumnMinWidth = 4.0f;
	constexpr float TotalTimeColumnMaxWidth = FLT_MAX;

	constexpr float TimeMsColumnInitialWidth = 50.0f;
	constexpr float TimeMsColumnMinWidth = 4.0f;
	constexpr float TimeMsColumnMaxWidth = FLT_MAX;

	//////////////////////////////////////////////////
	// Inclusive Time stats

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::TotalInclusiveTimeColumnID,
		TEXT("inc"),
		LOCTEXT("TotalInclusiveTimeName", "Incl"),
		LOCTEXT("TotalInclusiveTimeTitle", "Total Inclusive Time"),
		LOCTEXT("TotalInclusiveTimeDesc", "Total inclusive duration of selected timer's instances"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TotalTimeColumnInitialWidth, TotalTimeColumnMinWidth, TotalTimeColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeAuto(TimerNode.GetAggregatedStats().TotalInclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MaxInclusiveTimeColumnID,
		TEXT("maxinc"),
		LOCTEXT("MaxInclusiveTimeName", "I.Max"),
		LOCTEXT("MaxInclusiveTimeTitle", "Max Inclusive Time (ms)"),
		LOCTEXT("MaxInclusiveTimeDesc", "Maximum inclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MaxInclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::AverageInclusiveTimeColumnID,
		TEXT("avginc"),
		LOCTEXT("AvgInclusiveTimeName", "I.Avg"),
		LOCTEXT("AvgInclusiveTimeTitle", "Average Inclusive Time (ms)"),
		LOCTEXT("AvgInclusiveTimeDesc", "Average inclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().AverageInclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MedianInclusiveTimeColumnID,
		TEXT("medinc"),
		LOCTEXT("MedInclusiveTimeName", "I.Med"),
		LOCTEXT("MedInclusiveTimeTitle", "Median Inclusive Time (ms)"),
		LOCTEXT("MedInclusiveTimeDesc", "Median inclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MedianInclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MinInclusiveTimeColumnID,
		TEXT("mininc"),
		LOCTEXT("MinInclusiveTimeName", "I.Min"),
		LOCTEXT("MinInclusiveTimeTitle", "Min Inclusive Time (ms)"),
		LOCTEXT("MinInclusiveTimeDesc", "Minimum inclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MinInclusiveTime)); }
	));

	//////////////////////////////////////////////////
	// Exclusive Time stats

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::TotalExclusiveTimeColumnID,
		TEXT("exc"),
		LOCTEXT("TotalExclusiveTimeName", "Excl"),
		LOCTEXT("TotalExclusiveTimeTitle", "Total Exclusive Time"),
		LOCTEXT("TotalExclusiveTimeDesc", "Total exclusive duration of selected timer's instances"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TotalTimeColumnInitialWidth, TotalTimeColumnMinWidth, TotalTimeColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeAuto(TimerNode.GetAggregatedStats().TotalExclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MaxExclusiveTimeColumnID,
		TEXT("maxexc"),
		LOCTEXT("MaxExclusiveTimeName", "E.Max"),
		LOCTEXT("MaxExclusiveTimeTitle", "Max Exclusive Time (ms)"),
		LOCTEXT("MaxExclusiveTimeDesc", "Maximum exclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MaxExclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::AverageExclusiveTimeColumnID,
		TEXT("avgexc"),
		LOCTEXT("AvgExclusiveTimeName", "E.Avg"),
		LOCTEXT("AvgExclusiveTimeTitle", "Average Exclusive Time (ms)"),
		LOCTEXT("AvgExclusiveTimeDesc", "Average exclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().AverageExclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MedianExclusiveTimeColumnID,
		TEXT("medexc"),
		LOCTEXT("MedExclusiveTimeName", "E.Med"),
		LOCTEXT("MedExclusiveTimeTitle", "Median Exclusive Time (ms)"),
		LOCTEXT("MedExclusiveTimeDesc", "Median exclusive duration of timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MedianExclusiveTime)); }
	));

	Collection.Add(new FTimersViewColumn
	(
		Index++,
		FTimersViewColumns::MinExclusiveTimeColumnID,
		TEXT("minexc"),
		LOCTEXT("MinExclusiveTimeName", "E.Min"),
		LOCTEXT("MinExclusiveTimeTitle", "Min Exclusive Time (ms)"),
		LOCTEXT("MinExclusiveTimeDesc", "Minimum exclusive duration of selected timer's instances, in milliseconds"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
		[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
		{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetAggregatedStats().MinExclusiveTime)); }
	));

	//////////////////////////////////////////////////

	for (int32 MapIndex = 0; MapIndex < Collection.Num(); MapIndex++)
	{
		ColumnIdToPtrMapping.Add(Collection[MapIndex]->Id, Collection[MapIndex]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimersViewColumnFactory::~FTimersViewColumnFactory()
{
	for (const FTimersViewColumn* Column : Collection)
	{
		delete Column;
	}
	Collection.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FTimersViewColumnFactory& FTimersViewColumnFactory::Get()
{
	static FTimersViewColumnFactory Instance;
	return Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
