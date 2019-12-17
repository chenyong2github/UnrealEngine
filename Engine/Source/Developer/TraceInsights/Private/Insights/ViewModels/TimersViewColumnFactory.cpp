// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimersViewColumnFactory.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/ViewModels/TimerGroupingAndSorting.h"
#include "Insights/ViewModels/TimerNodeHelper.h"

#define LOCTEXT_NAMESPACE "STimerView"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FTimersViewColumns::NameColumnID(TEXT("Name")); // TEXT("_Hierarchy")
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

void FTimersViewColumnFactory::CreateTimersViewColumns(TArray<TSharedRef<Insights::FTableColumn>>& Columns)
{
	Columns.Reset();

	Columns.Add(CreateNameColumn());
	Columns.Add(CreateMetaGroupNameColumn());
	Columns.Add(CreateTypeColumn());
	Columns.Add(CreateInstanceCountColumn());

	Columns.Add(CreateTotalInclusiveTimeColumn());
	Columns.Add(CreateMaxInclusiveTimeColumn());
	Columns.Add(CreateAverageInclusiveTimeColumn());
	Columns.Add(CreateMedianInclusiveTimeColumn());
	Columns.Add(CreateMinInclusiveTimeColumn());

	Columns.Add(CreateTotalExclusiveTimeColumn());
	Columns.Add(CreateMaxExclusiveTimeColumn());
	Columns.Add(CreateAverageExclusiveTimeColumn());
	Columns.Add(CreateMedianExclusiveTimeColumn());
	Columns.Add(CreateMinExclusiveTimeColumn());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimersViewColumnFactory::CreateTimerTreeViewColumns(TArray<TSharedRef<Insights::FTableColumn>>& Columns)
{
	Columns.Reset();

	Columns.Add(CreateNameColumn());
	Columns.Add(CreateTypeColumn());
	Columns.Add(CreateInstanceCountColumn());
	Columns.Add(CreateTotalInclusiveTimeColumn());
	Columns.Add(CreateTotalExclusiveTimeColumn());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateNameColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::NameColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("TimerNameColumnName", "Name"));
	Column.SetTitleName(LOCTEXT("TimerNameColumnTitle", "Timer or Group Name"));
	Column.SetDescription(LOCTEXT("TimerNameColumnDesc", "Name of the timer or group"));

	Column.SetFlags(ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered |
					ETableColumnFlags::IsHierarchy);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(246.0f);
	Column.SetMinWidth(42.0f);

	Column.SetDataType(ETableCellDataType::Text);

	TSharedRef<ITableCellValueGetter> Getter = MakeShared<FDisplayNameValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FTextValueFormatter>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByName>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMetaGroupNameColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MetaGroupNameColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("TimerMetaGroupNameColumnName", "Meta Group"));
	Column.SetTitleName(LOCTEXT("TimerMetaGroupNameColumnTitle", "Meta Group Name"));
	Column.SetDescription(LOCTEXT("TimerMetaGroupNameColumnDesc", "Name of the meta group"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(100.0f);

	Column.SetDataType(ETableCellDataType::Text);

	class FMetaGroupNameValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MetaGroupNameColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(FText::FromName(TimerNode.GetMetaGroupName())));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMetaGroupNameValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FTextValueFormatter>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByTextValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTypeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::TypeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("TimerTypeColumnName", "Type"));
	Column.SetTitleName(LOCTEXT("TimerTypeColumnTitle", "Type"));
	Column.SetDescription(LOCTEXT("TimerTypeColumnDesc", "Type of timer or group"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(60.0f);

	Column.SetDataType(ETableCellDataType::Text);

	class FTimerTypeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::TypeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNodeTypeHelper::ToText(TimerNode.GetType())));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FTimerTypeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FTextValueFormatter>();
	Column.SetValueFormatter(Formatter);

	//TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByTextValue>(ColumnRef);
	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FTimerNodeSortingByTimerType>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateInstanceCountColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::InstanceCountColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("InstanceCountName", "Count"));
	Column.SetTitleName(LOCTEXT("InstanceCountTitle", "Instance Count"));
	Column.SetDescription(LOCTEXT("InstanceCountDesc", "Number of timer's instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(60.0f);

	Column.SetDataType(ETableCellDataType::Int64);

	class FInstanceCountValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::InstanceCountColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(TimerNode.GetAggregatedStats().InstanceCount)));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FInstanceCountValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
	Column.SetValueFormatter(Formatter);

	//TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FTimerNodeSortingByInstanceCount>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Inclusive Time Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTotalInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::TotalInclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("TotalInclusiveTimeName", "Incl"));
	Column.SetTitleName(LOCTEXT("TotalInclusiveTimeTitle", "Total Inclusive Time"));
	Column.SetDescription(LOCTEXT("TotalInclusiveTimeDesc", "Total inclusive duration of selected timer's instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TotalTimeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);
	Column.SetAggregation(ETableColumnAggregation::Sum);

	class FTotalInclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::TotalInclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().TotalInclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FTotalInclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeAuto>();
	Column.SetValueFormatter(Formatter);

	//TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FTimerNodeSortingByTotalInclusiveTime>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMaxInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MaxInclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MaxInclusiveTimeName", "I.Max"));
	Column.SetTitleName(LOCTEXT("MaxInclusiveTimeTitle", "Max Inclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MaxInclusiveTimeDesc", "Maximum inclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMaxInclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MaxInclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MaxInclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMaxInclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateAverageInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::AverageInclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("AvgInclusiveTimeName", "I.Avg"));
	Column.SetTitleName(LOCTEXT("AvgInclusiveTimeTitle", "Average Inclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("AvgInclusiveTimeDesc", "Average inclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FAverageInclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::AverageInclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().AverageInclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FAverageInclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMedianInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MedianInclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MedInclusiveTimeName", "I.Med"));
	Column.SetTitleName(LOCTEXT("MedInclusiveTimeTitle", "Median Inclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MedInclusiveTimeDesc", "Median inclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMedianInclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MedianInclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MedianInclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMedianInclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMinInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MinInclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MinInclusiveTimeName", "I.Min"));
	Column.SetTitleName(LOCTEXT("MinInclusiveTimeTitle", "Min Inclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MinInclusiveTimeDesc", "Minimum inclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMinInclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MinInclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MinInclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMinInclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Exclusive Time Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTotalExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::TotalExclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("TotalExclusiveTimeName", "Excl"));
	Column.SetTitleName(LOCTEXT("TotalExclusiveTimeTitle", "Total Exclusive Time"));
	Column.SetDescription(LOCTEXT("TotalExclusiveTimeDesc", "Total exclusive duration of selected timer's instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TotalTimeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);
	Column.SetAggregation(ETableColumnAggregation::Sum);

	class FTotalExclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::TotalExclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().TotalExclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FTotalExclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeAuto>();
	Column.SetValueFormatter(Formatter);

	//TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FTimerNodeSortingByTotalExclusiveTime>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMaxExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MaxExclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MaxExclusiveTimeName", "E.Max"));
	Column.SetTitleName(LOCTEXT("MaxExclusiveTimeTitle", "Max Exclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MaxExclusiveTimeDesc", "Maximum exclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMaxExclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MaxExclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MaxExclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMaxExclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateAverageExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::AverageExclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("AvgExclusiveTimeName", "E.Avg"));
	Column.SetTitleName(LOCTEXT("AvgExclusiveTimeTitle", "Average Exclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("AvgExclusiveTimeDesc", "Average exclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FAverageExclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::AverageExclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().AverageExclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FAverageExclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMedianExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MedianExclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MedExclusiveTimeName", "E.Med"));
	Column.SetTitleName(LOCTEXT("MedExclusiveTimeTitle", "Median Exclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MedExclusiveTimeDesc", "Median exclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMedianExclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MedianExclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MedianExclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMedianExclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMinExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FTimersViewColumns::MinExclusiveTimeColumnID);
	FTableColumn& Column = *ColumnRef;

	Column.SetShortName(LOCTEXT("MinExclusiveTimeName", "E.Min"));
	Column.SetTitleName(LOCTEXT("MinExclusiveTimeTitle", "Min Exclusive Time (ms)"));
	Column.SetDescription(LOCTEXT("MinExclusiveTimeDesc", "Minimum exclusive duration of selected timer's instances, in milliseconds"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TimeMsColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Double);

	class FMinExclusiveTimeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FTimersViewColumns::MinExclusiveTimeColumnID);
			const FTimerNode& TimerNode = static_cast<const FTimerNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(TimerNode.GetAggregatedStats().MinExclusiveTime));
		}
	};

	TSharedRef<FTableCellValueGetter> Getter = MakeShared<FMinExclusiveTimeValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<FTableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeMs>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
	Column.SetValueSorter(Sorter);

	return ColumnRef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
