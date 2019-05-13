// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimerNodeHelper.h"
#include "Insights/ViewModels/TimersViewColumn.h"

#define LOCTEXT_NAMESPACE "STimerView"

struct FTimersViewColumnFactory
{
	/** Default constructor. */
	FTimersViewColumnFactory()
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

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("Name"),
			TEXT("name"),
			LOCTEXT("TimerNameColumnName", "Name"),
			LOCTEXT("TimerNameColumnTitle", "Timer or Group Name"),
			LOCTEXT("TimerNameColumnDesc", "Name of the timer or group."),
			true,
			FTimersViewColumn::EFlags::NameColumnFlags,
			HAlign_Left,
			246.0f, 10.0f, FLT_MAX,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromName(TimerNode.GetName()); }
		));

		//////////////////////////////////////////////////
		// Meta Group Name

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MetaGroupName"),
			TEXT("metagroupname"),
			LOCTEXT("TimerMetaGroupNameColumnName", "Meta Group"),
			LOCTEXT("TimerMetaGroupNameColumnTitle", "Meta Group Name"),
			LOCTEXT("TimerMetaGroupNameColumnDesc", "Name of the meta group."),
			false,
			(FTimersViewColumn::EFlags)(FTimersViewColumn::EFlags::CanBeHidden |
										FTimersViewColumn::EFlags::CanBeSorted |
										FTimersViewColumn::EFlags::CanBeFiltered),
			HAlign_Left,
			100.0f, 10.0f, FLT_MAX,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromName(TimerNode.GetMetaGroupName()); }
		));

		//////////////////////////////////////////////////
		// Type

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("Type"),
			TEXT("type"),
			LOCTEXT("TimerTypeColumnName", "Type"),
			LOCTEXT("TimerTypeColumnTitle", "Type"),
			LOCTEXT("TimerTypeColumnDesc", "Type of timer or group."),
			false,
			(FTimersViewColumn::EFlags)(FTimersViewColumn::EFlags::CanBeHidden |
										FTimersViewColumn::EFlags::CanBeSorted |
										FTimersViewColumn::EFlags::CanBeFiltered),
			HAlign_Left,
			60.0f, 10.0f, FLT_MAX,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return TimerNodeTypeHelper::ToName(TimerNode.GetType()); }
		));

		//////////////////////////////////////////////////
		// Instance Count

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("InstanceCount"),
			TEXT("count"),
			LOCTEXT("InstanceCountName", "Count"),
			LOCTEXT("InstanceCountTitle", "Instance Count"),
			LOCTEXT("InstanceCountDesc", "Number of timer's instances"),
			true,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			60.0f, 0.0f, FLT_MAX,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::AsNumber(TimerNode.GetStats().InstanceCount); }
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
			TEXT("TotalInclusiveTime"),
			TEXT("inc"),
			LOCTEXT("TotalInclusiveTimeName", "Incl."),
			LOCTEXT("TotalInclusiveTimeTitle", "Total Inclusive Time"),
			LOCTEXT("TotalInclusiveTimeDesc", "Total inclusive duration of selected timer's instances."),
			true,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TotalTimeColumnInitialWidth, TotalTimeColumnMinWidth, TotalTimeColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeAuto(TimerNode.GetStats().TotalInclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MinInclusiveTime"),
			TEXT("mininc"),
			LOCTEXT("MinInclusiveTimeName", "I.Min"),
			LOCTEXT("MinInclusiveTimeTitle", "Min Inclusive Time (ms)"),
			LOCTEXT("MinInclusiveTimeDesc", "Minimum inclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MinInclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MaxInclusiveTime"),
			TEXT("maxinc"),
			LOCTEXT("MaxInclusiveTimeName", "I.Max"),
			LOCTEXT("MaxInclusiveTimeTitle", "Max Inclusive Time (ms)"),
			LOCTEXT("MaxInclusiveTimeDesc", "Maximum inclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MaxInclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("AvgInclusiveTime"),
			TEXT("avginc"),
			LOCTEXT("AvgInclusiveTimeName", "I.Avg."),
			LOCTEXT("AvgInclusiveTimeTitle", "Average Inclusive Time (ms)"),
			LOCTEXT("AvgInclusiveTimeDesc", "Average inclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().AverageInclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MedInclusiveTime"),
			TEXT("medinc"),
			LOCTEXT("MedInclusiveTimeName", "I.Med."),
			LOCTEXT("MedInclusiveTimeTitle", "Median Inclusive Time (ms)"),
			LOCTEXT("MedInclusiveTimeDesc", "Median inclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MedianInclusiveTime)); }
		));

		//////////////////////////////////////////////////
		// Exclusive Time stats

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("TotalExclusiveTime"),
			TEXT("exc"),
			LOCTEXT("TotalExclusiveTimeName", "Excl."),
			LOCTEXT("TotalExclusiveTimeTitle", "Total Exclusive Time"),
			LOCTEXT("TotalExclusiveTimeDesc", "Total exclusive duration of selected timer's instances."),
			true,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TotalTimeColumnInitialWidth, TotalTimeColumnMinWidth, TotalTimeColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeAuto(TimerNode.GetStats().TotalExclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MinExclusiveTime"),
			TEXT("minexc"),
			LOCTEXT("MinExclusiveTimeName", "E.Min"),
			LOCTEXT("MinExclusiveTimeTitle", "Min Exclusive Time (ms)"),
			LOCTEXT("MinExclusiveTimeDesc", "Minimum exclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MinExclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MaxExclusiveTime"),
			TEXT("maxexc"),
			LOCTEXT("MaxExclusiveTimeName", "E.Max"),
			LOCTEXT("MaxExclusiveTimeTitle", "Max Exclusive Time (ms)"),
			LOCTEXT("MaxExclusiveTimeDesc", "Maximum exclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MaxExclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("AvgExclusiveTime"),
			TEXT("avgexc"),
			LOCTEXT("AvgExclusiveTimeName", "E.Avg."),
			LOCTEXT("AvgExclusiveTimeTitle", "Average Exclusive Time (ms)"),
			LOCTEXT("AvgExclusiveTimeDesc", "Average exclusive duration of selected timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().AverageExclusiveTime)); }
		));

		Collection.Add(new FTimersViewColumn
		(
			Index++,
			TEXT("MedExclusiveTime"),
			TEXT("medexc"),
			LOCTEXT("MedExclusiveTimeName", "E.Med."),
			LOCTEXT("MedExclusiveTimeTitle", "Median Exclusive Time (ms)"),
			LOCTEXT("MedExclusiveTimeDesc", "Median exclusive duration of timer's instances, in milliseconds"),
			false,
			FTimersViewColumn::EFlags::StatsColumnFlags,
			HAlign_Right,
			TimeMsColumnInitialWidth, TimeMsColumnMinWidth, TimeMsColumnMaxWidth,
			[](const FTimersViewColumn& Column, const FTimerNode& TimerNode) -> FText
			{ return FText::FromString(TimeUtils::FormatTimeMs(TimerNode.GetStats().MedianExclusiveTime)); }
		));

		//////////////////////////////////////////////////

		for (int32 MapIndex = 0; MapIndex < Collection.Num(); MapIndex++)
		{
			ColumnIdToPtrMapping.Add(Collection[MapIndex]->Id, Collection[MapIndex]);
		}
	}

	~FTimersViewColumnFactory()
	{
		for (const FTimersViewColumn* Column : Collection)
			delete Column;
		Collection.Reset();
	}

	/** Contains basic information about columns used in the timer view widget. Names should be localized. */
	TArray<FTimersViewColumn*> Collection;

	TMap<FName, const FTimersViewColumn*> ColumnIdToPtrMapping;

	static const FTimersViewColumnFactory& Get()
	{
		static FTimersViewColumnFactory Instance;
		return Instance;
	}
};

#undef LOCTEXT_NAMESPACE
