// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetStatsViewColumnFactory.h"

// Insights
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/NetworkingProfiler/ViewModels/NetEventGroupingAndSorting.h"
#include "Insights/NetworkingProfiler/ViewModels/NetEventNodeHelper.h"

#define LOCTEXT_NAMESPACE "SNetStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FNetStatsViewColumns::NameColumnID(TEXT("Name")); // TEXT("_Hierarchy")
const FName FNetStatsViewColumns::TypeColumnID(TEXT("Type"));
const FName FNetStatsViewColumns::LevelColumnID(TEXT("Level"));
const FName FNetStatsViewColumns::InstanceCountColumnID(TEXT("Count"));

// Inclusive  columns
const FName FNetStatsViewColumns::TotalInclusiveSizeColumnID(TEXT("TotalIncl"));
const FName FNetStatsViewColumns::MaxInclusiveSizeColumnID(TEXT("MaxIncl"));
const FName FNetStatsViewColumns::AverageInclusiveSizeColumnID(TEXT("AverageIncl"));

// Exclusive  columns
const FName FNetStatsViewColumns::TotalExclusiveSizeColumnID(TEXT("TotalExcl"));
const FName FNetStatsViewColumns::MaxExclusiveSizeColumnID(TEXT("MaxExcl"));
const FName FNetStatsViewColumns::AverageExclusiveSizeColumnID(TEXT("AverageExcl"));

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetStatsViewColumnFactory::CreateNetStatsViewColumns(TArray<TSharedPtr<Insights::FTableColumn>>& Columns)
{
	Columns.Reset();

	Columns.Add(CreateNameColumn());
	Columns.Add(CreateTypeColumn());
	Columns.Add(CreateLevelColumn());
	Columns.Add(CreateInstanceCountColumn());

	Columns.Add(CreateTotalInclusiveSizeColumn());
	Columns.Add(CreateMaxInclusiveSizeColumn());
	Columns.Add(CreateAverageInclusiveSizeColumn());

	Columns.Add(CreateTotalExclusiveSizeColumn());
	Columns.Add(CreateMaxExclusiveSizeColumn());
	//Columns.Add(CreateAverageExclusiveSizeColumn());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateNameColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::NameColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("Name_ColumnName", "Name"));
	Column.SetTitleName(LOCTEXT("Name_ColumnTitle", "NetEvent or Group Name"));
	Column.SetDescription(LOCTEXT("Name_ColumnDesc", "Name of the timer or group"));

	Column.SetFlags(ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered |
					ETableColumnFlags::IsHierarchy);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(206.0f);
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

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateTypeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::TypeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("Type_ColumnName", "Type"));
	Column.SetTitleName(LOCTEXT("Type_ColumnTitle", "Type"));
	Column.SetDescription(LOCTEXT("Type_ColumnDesc", "Type of timer or group"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(60.0f);

	Column.SetDataType(ETableCellDataType::Text);

	class FNetEventTypeValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::TypeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(NetEventNodeTypeHelper::ToText(NetEventNode.GetType())));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FNetEventTypeValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FTextValueFormatter());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByTextValue(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FNetEventNodeSortingByEventType(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateLevelColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::LevelColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("Level_ColumnName", "Level"));
	Column.SetTitleName(LOCTEXT("Level_ColumnTitle", "Level"));
	Column.SetDescription(LOCTEXT("Level_ColumnDesc", "Level of net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(40.0f);

	Column.SetDataType(ETableCellDataType::Int64);

	class FLevelValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::LevelColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetLevel())));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FLevelValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateInstanceCountColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::InstanceCountColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("InstanceCount_ColumnName", "Count"));
	Column.SetTitleName(LOCTEXT("InstanceCount_ColumnTitle", "Instance Count"));
	Column.SetDescription(LOCTEXT("InstanceCount_ColumnDesc", "Number of net event instances"));

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
			ensure(Column.GetId() == FNetStatsViewColumns::InstanceCountColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().InstanceCount)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FInstanceCountValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FNetEventNodeSortingByInstanceCount(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Inclusive  Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateTotalInclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::TotalInclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("TotalInclusive_ColumnName", "Incl"));
	Column.SetTitleName(LOCTEXT("TotalInclusive_ColumnTitle", "Total Inclusive Size"));
	Column.SetDescription(LOCTEXT("TotalInclusive_ColumnDesc", "Total inclusive size (bits) of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TotalSizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);
	Column.SetAggregation(ETableColumnAggregation::Sum);

	class FTotalInclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::TotalInclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().TotalInclusive)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FTotalInclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FNetEventNodeSortingByTotalInclusiveSize(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateMaxInclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::MaxInclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("MaxInclusive_ColumnName", "I.Max"));
	Column.SetTitleName(LOCTEXT("MaxInclusive_ColumnTitle", "Max Inclusive Size"));
	Column.SetDescription(LOCTEXT("MaxInclusive_ColumnDesc", "Maximum inclusive size (bits) of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(SizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);

	class FMaxInclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::MaxInclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().MaxInclusive)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMaxInclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateAverageInclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::AverageInclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("AvgInclusive_ColumnName", "I.Avg"));
	Column.SetTitleName(LOCTEXT("AvgInclusive_ColumnTitle", "Average Inclusive Size"));
	Column.SetDescription(LOCTEXT("AvgInclusive_ColumnDesc", "Average inclusive size (bits) of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(SizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);

	class FAverageInclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::AverageInclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().AverageInclusive)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FAverageInclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Exclusive  Columns
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateTotalExclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::TotalExclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("TotalExclusive_ColumnName", "Excl"));
	Column.SetTitleName(LOCTEXT("TotalExclusive_ColumnTitle", "Total Exclusive Size"));
	Column.SetDescription(LOCTEXT("TotalExclusive_ColumnDesc", "Total exclusive size (bits) of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(TotalSizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);
	Column.SetAggregation(ETableColumnAggregation::Sum);

	class FTotalExclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::TotalExclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().TotalExclusive)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FTotalExclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	//TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FNetEventNodeSortingByTotalExclusiveSize(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateMaxExclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::MaxExclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("MaxExclusive_ColumnName", "E.Max"));
	Column.SetTitleName(LOCTEXT("MaxExclusive_ColumnTitle", "Max Exclusive Size"));
	Column.SetDescription(LOCTEXT("MaxExclusive_ColumnDesc", "Maximum exclusive size of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(SizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);

	class FMaxExclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::MaxExclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().MaxExclusive)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FMaxExclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<Insights::FTableColumn> FNetStatsViewColumnFactory::CreateAverageExclusiveSizeColumn()
{
	using namespace Insights;

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FNetStatsViewColumns::AverageExclusiveSizeColumnID));
	FTableColumn& Column = *ColumnPtr;

	Column.SetShortName(LOCTEXT("AvgExclusiveName", "E.Avg"));
	Column.SetTitleName(LOCTEXT("AvgExclusiveTitle", "Average Exclusive  (ms)"));
	Column.SetDescription(LOCTEXT("AvgExclusiveDesc", "Average exclusive size of selected net event instances"));

	Column.SetFlags(ETableColumnFlags::CanBeHidden |
					//ETableColumnFlags::ShouldBeVisible |
					ETableColumnFlags::CanBeFiltered);

	Column.SetHorizontalAlignment(HAlign_Right);
	Column.SetInitialWidth(SizeColumnInitialWidth);

	Column.SetDataType(ETableCellDataType::Int64);

	class FAverageExclusiveValueGetter : public FTableCellValueGetter
	{
	public:
		virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
		{
			ensure(Column.GetId() == FNetStatsViewColumns::AverageExclusiveSizeColumnID);
			const FNetEventNode& NetEventNode = static_cast<const FNetEventNode&>(Node);
			//return TOptional<FTableCellValue>(FTableCellValue(static_cast<int64>(NetEventNode.GetAggregatedStats().AverageExclusive)));
			return TOptional<FTableCellValue>(FTableCellValue(int64(0)));
		}
	};

	TSharedPtr<FTableCellValueGetter> GetterPtr = MakeShareable(new FAverageExclusiveValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	return ColumnPtr.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
