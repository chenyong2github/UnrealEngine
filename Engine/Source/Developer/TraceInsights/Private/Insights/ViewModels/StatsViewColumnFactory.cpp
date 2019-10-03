// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StatsViewColumnFactory.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/StatsNodeHelper.h"
#include "Insights/ViewModels/StatsViewColumn.h"

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

// Column identifiers
const FName FStatsViewColumns::NameColumnID(TEXT("Name"));
const FName FStatsViewColumns::MetaGroupNameColumnID(TEXT("MetaGroupName"));
const FName FStatsViewColumns::TypeColumnID(TEXT("Type"));
const FName FStatsViewColumns::CountColumnID(TEXT("Count"));
const FName FStatsViewColumns::SumColumnID(TEXT("Sum"));
const FName FStatsViewColumns::MaxColumnID(TEXT("Max"));
const FName FStatsViewColumns::UpperQuartileColumnID(TEXT("UpperQuartile"));
const FName FStatsViewColumns::AverageColumnID(TEXT("Average"));
const FName FStatsViewColumns::MedianColumnID(TEXT("Median"));
const FName FStatsViewColumns::LowerQuartileColumnID(TEXT("LowerQuartile"));
const FName FStatsViewColumns::MinColumnID(TEXT("Min"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FStatsViewColumnFactory::FStatsViewColumnFactory()
{
	uint32 Index = 0;

	// FStatsViewColumn
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

	const EStatsViewColumnFlags NameColumnFlags = EStatsViewColumnFlags::CanBeSorted |
												  EStatsViewColumnFlags::CanBeFiltered;

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::NameColumnID,
		TEXT("name"),
		LOCTEXT("StatsNameColumnName", "Name"),
		LOCTEXT("StatsNameColumnTitle", "Stats or Group Name"),
		LOCTEXT("StatsNameColumnDesc", "Name of the stats or group"),
		true,
		NameColumnFlags,
		HAlign_Left,
		206.0f, 10.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return FText::FromName(StatsNode.GetName()); }
	));

	//////////////////////////////////////////////////
	// Meta Group Name

	const EStatsViewColumnFlags MetaGroupNameColumnFlags = EStatsViewColumnFlags::CanBeHidden |
														   EStatsViewColumnFlags::CanBeSorted |
														   EStatsViewColumnFlags::CanBeFiltered;

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::MetaGroupNameColumnID,
		TEXT("metagroupname"),
		LOCTEXT("StatsMetaGroupNameColumnName", "Meta Group"),
		LOCTEXT("StatsMetaGroupNameColumnTitle", "Meta Group Name"),
		LOCTEXT("StatsMetaGroupNameColumnDesc", "Name of the meta group"),
		false,
		MetaGroupNameColumnFlags,
		HAlign_Left,
		100.0f, 10.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return FText::FromName(StatsNode.GetMetaGroupName()); }
	));

	//////////////////////////////////////////////////
	// Type

	const EStatsViewColumnFlags TypeColumnFlags = EStatsViewColumnFlags::CanBeHidden |
												  EStatsViewColumnFlags::CanBeSorted |
												  EStatsViewColumnFlags::CanBeFiltered;

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::TypeColumnID,
		TEXT("type"),
		LOCTEXT("StatsTypeColumnName", "Type"),
		LOCTEXT("StatsTypeColumnTitle", "Type"),
		LOCTEXT("StatsTypeColumnDesc", "Type of the stats or group"),
		false,
		TypeColumnFlags,
		HAlign_Left,
		60.0f, 10.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNodeTypeHelper::ToName(StatsNode.GetType()); }
	));

	//////////////////////////////////////////////////

	const EStatsViewColumnFlags AggregatedStatsColumnFlags = EStatsViewColumnFlags::CanBeHidden |
															 EStatsViewColumnFlags::CanBeSorted |
															 EStatsViewColumnFlags::CanBeFiltered;

	//////////////////////////////////////////////////
	// Count

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::CountColumnID,
		TEXT("count"),
		LOCTEXT("CountName", "Count"),
		LOCTEXT("CountTitle", "Count"),
		LOCTEXT("CountDesc", "Number of selected values"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		60.0f, 0.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return FText::AsNumber(StatsNode.GetAggregatedStats().Count); }
	));

	//////////////////////////////////////////////////
	// Aggregated stats

	const float StatsInitialColumnWidth = 80.0f;

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::SumColumnID,
		TEXT("sum"),
		LOCTEXT("SumName", "Sum"),
		LOCTEXT("SumTitle", "Sum"),
		LOCTEXT("SumDesc", "Sum of selected values"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsSum(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::MaxColumnID,
		TEXT("max"),
		LOCTEXT("MaxName", "Max"),
		LOCTEXT("MaxTitle", "Maximum"),
		LOCTEXT("MaxDesc", "Maximum of selected values"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsMax(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::UpperQuartileColumnID,
		TEXT("upperquartile"),
		LOCTEXT("UpperQuartileName", "Upper"),
		LOCTEXT("UpperQuartileTitle", "Upper Quartile"),
		LOCTEXT("UpperQuartileDesc", "Upper quartile (Q3; third quartile; 75th percentile) of selected values"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsUpperQuartile(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::AverageColumnID,
		TEXT("avg"),
		LOCTEXT("AverageName", "Avg."),
		LOCTEXT("AverageTitle", "Average"),
		LOCTEXT("AverageDesc", "Average (arithmetic mean) of selected values"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsAverage(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::MedianColumnID,
		TEXT("med"),
		LOCTEXT("MedianName", "Med."),
		LOCTEXT("MedianTitle", "Median"),
		LOCTEXT("MedianDesc", "Median (Q2; second quartile; 50th percentile) of selected values"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsMedian(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::LowerQuartileColumnID,
		TEXT("lowerquartile"),
		LOCTEXT("LowerQuartileName", "Lower"),
		LOCTEXT("LowerQuartileTitle", "Lower Quartile"),
		LOCTEXT("LowerQuartileDesc", "Lower quartile (Q1; first quartile; 25th percentile) of selected values"),
		false,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsLowerQuartile(); }
	));

	Collection.Add(new FStatsViewColumn
	(
		Index++,
		FStatsViewColumns::MinColumnID,
		TEXT("min"),
		LOCTEXT("MinName", "Min"),
		LOCTEXT("MinTitle", "Minimum"),
		LOCTEXT("MinDesc", "Minimum of selected values"),
		true,
		AggregatedStatsColumnFlags,
		HAlign_Right,
		StatsInitialColumnWidth, 4.0f, FLT_MAX,
		[](const FStatsViewColumn& Column, const FStatsNode& StatsNode) -> FText
		{ return StatsNode.GetTextForAggregatedStatsMin(); }
	));

	//////////////////////////////////////////////////

	for (int32 MapIndex = 0; MapIndex < Collection.Num(); MapIndex++)
	{
		ColumnIdToPtrMapping.Add(Collection[MapIndex]->Id, Collection[MapIndex]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FStatsViewColumnFactory::~FStatsViewColumnFactory()
{
	for (const FStatsViewColumn* Column : Collection)
	{
		delete Column;
	}
	Collection.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FStatsViewColumnFactory& FStatsViewColumnFactory::Get()
{
	static FStatsViewColumnFactory Instance;
	return Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
