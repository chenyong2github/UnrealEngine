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

void FTimersViewColumnFactory::CreateTimersViewColumns(TArray<TSharedPtr<Insights::FTableColumn>>& Columns)
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

void FTimersViewColumnFactory::CreateTimerTreeViewColumns(TArray<TSharedPtr<Insights::FTableColumn>>& Columns)
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

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::NameColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<ITableCellValueGetter> GetterPtr = MakeShareable(new FDisplayNameValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueFormatter> FormatterPtr = MakeShareable(new FTextValueFormatter());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByName(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMetaGroupNameColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MetaGroupNameColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMetaGroupNameValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FTextValueFormatter());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByTextValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTypeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::TypeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FTimerTypeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FTextValueFormatter());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByTextValue(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FTimerNodeSortingByTimerType(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateInstanceCountColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::InstanceCountColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FInstanceCountValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FTimerNodeSortingByInstanceCount(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Inclusive Time Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTotalInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::TotalInclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FTotalInclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeAuto());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FTimerNodeSortingByTotalInclusiveTime(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMaxInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MaxInclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMaxInclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateAverageInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::AverageInclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FAverageInclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMedianInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MedianInclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMedianInclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMinInclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MinInclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMinInclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Exclusive Time Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateTotalExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::TotalExclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FTotalExclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeAuto());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FTimerNodeSortingByTotalExclusiveTime(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMaxExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MaxExclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMaxExclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateAverageExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::AverageExclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FAverageExclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMedianExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MedianExclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMedianExclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FTimersViewColumnFactory::CreateMinExclusiveTimeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FTimersViewColumns::MinExclusiveTimeColumnID));
	FTableColumn& Column = *ColumnPtr;

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

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMinExclusiveTimeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeMs());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
