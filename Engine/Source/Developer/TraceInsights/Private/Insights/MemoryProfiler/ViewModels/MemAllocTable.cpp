// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocTable.h"
#include "CallstackFormatting.h"
#include "Containers/StringView.h"
#include "Internationalization/Regex.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocFilterValueConverter.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/ViewModels/TimeFilterValueConverter.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocTable"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FMemAllocTableColumns::StartEventIndexColumnId(TEXT("StartEventIndex"));
const FName FMemAllocTableColumns::EndEventIndexColumnId(TEXT("EndEventIndex"));
const FName FMemAllocTableColumns::EventDistanceColumnId(TEXT("EventDistance"));
const FName FMemAllocTableColumns::StartTimeColumnId(TEXT("StartTime"));
const FName FMemAllocTableColumns::EndTimeColumnId(TEXT("EndTime"));
const FName FMemAllocTableColumns::DurationColumnId(TEXT("Duration"));
const FName FMemAllocTableColumns::AddressColumnId(TEXT("Address"));
const FName FMemAllocTableColumns::MemoryPageColumnId(TEXT("Memory Page"));
const FName FMemAllocTableColumns::CountColumnId(TEXT("Count"));
const FName FMemAllocTableColumns::SizeColumnId(TEXT("Size"));
const FName FMemAllocTableColumns::TagColumnId(TEXT("Tag"));
const FName FMemAllocTableColumns::FunctionColumnId(TEXT("Function"));

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemAllocTable
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocTable::FMemAllocTable()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocTable::~FMemAllocTable()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocTable::Reset()
{
	//...

	FTable::Reset();

	AddDefaultColumns();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocTable::AddDefaultColumns()
{
	//////////////////////////////////////////////////
	// Hierarchy Column
	{
		const int32 HierarchyColumnIndex = -1;
		const TCHAR* HierarchyColumnName = nullptr;
		AddHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

		const TSharedRef<FTableColumn>& ColumnRef = GetColumns()[0];
		ColumnRef->SetInitialWidth(200.0f);
		ColumnRef->SetShortName(LOCTEXT("AllocationColumnName", "Hierarchy"));
		ColumnRef->SetTitleName(LOCTEXT("AllocationColumnTitle", "Allocation Hierarchy"));
		ColumnRef->SetDescription(LOCTEXT("AllocationColumnDesc", "Hierarchy of the allocation's tree"));
	}

	int32 ColumnIndex = 0;

	//////////////////////////////////////////////////
	// Start Event Index Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::StartEventIndexColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("StartEventIndexColumnName", "Start Index"));
		Column.SetTitleName(LOCTEXT("StartEventIndexColumnTitle", "Start Event Index"));
		Column.SetDescription(LOCTEXT("StartEventIndexColumnDesc", "The event index when the allocation was allocated."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocStartEventIndexValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetStartEventIndex());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocStartEventIndexValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::Min);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// End Event Index Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::EndEventIndexColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("EndEventIndexColumnName", "End Index"));
		Column.SetTitleName(LOCTEXT("EndEventIndexColumnTitle", "End Event Index"));
		Column.SetDescription(LOCTEXT("EndEventIndexColumnDesc", "The event index when the allocation was freed."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocEndEventIndexValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetEndEventIndex());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocEndEventIndexValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::Max);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Event Distance Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::EventDistanceColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("EventDistanceColumnName", "Event Distance"));
		Column.SetTitleName(LOCTEXT("EventDistanceColumnTitle", "Event Distance"));
		Column.SetDescription(LOCTEXT("EventDistanceColumnDesc", "The event distance (index of free event minus index of alloc event)."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocEventDistanceValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetEventDistance());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocEventDistanceValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::Max);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Start Time Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::StartTimeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("StartTimeColumnName", "Start Time"));
		Column.SetTitleName(LOCTEXT("StartTimeColumnTitle", "Start Time"));
		Column.SetDescription(LOCTEXT("StartTimeColumnDesc", "The time when the allocation was allocated."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Double);

		class FMemAllocStartTimeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetStartTime());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocStartTimeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeAuto>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		TSharedRef<IFilterValueConverter> Converter = MakeShared<FTimeFilterValueConverter>();
		Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Min);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// End Time Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::EndTimeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("EndTimeColumnName", "End Time"));
		Column.SetTitleName(LOCTEXT("EndTimeColumnTitle", "End Time"));
		Column.SetDescription(LOCTEXT("EndTimeColumnDesc", "The time when the allocation was freed."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Double);

		class FMemAllocEndTimeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetEndTime());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocEndTimeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeAuto>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		TSharedRef<IFilterValueConverter> Converter = MakeShared<FTimeFilterValueConverter>();
		Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Max);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Duration Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::DurationColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("DurationColumnName", "Duration"));
		Column.SetTitleName(LOCTEXT("DurationColumnTitle", "Duration"));
		Column.SetDescription(LOCTEXT("DurationColumnDesc", "The duration of the allocation's life."));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Double);

		class FMemAllocDurationValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetDuration());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocDurationValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FDoubleValueFormatterAsTimeAuto>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByDoubleValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		TSharedRef<IFilterValueConverter> Converter = MakeShared<FTimeFilterValueConverter>();
		Column.SetValueConverter(Converter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Address Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::AddressColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("AddressColumnName", "Address"));
		Column.SetTitleName(LOCTEXT("AddressColumnTitle", "Address"));
		Column.SetDescription(LOCTEXT("AddressColumnDesc", "Address of allocation"));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocAddressValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(static_cast<int64>(Alloc->GetAddress()));
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocAddressValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsHex64>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Memory Page Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::MemoryPageColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("MemoryPageColumnName", "Memory Page"));
		Column.SetTitleName(LOCTEXT("MemoryPageColumnTitle", "Memory Page"));
		Column.SetDescription(LOCTEXT("MemoryPageColumnDesc", "Memory Page of allocation"));

		Column.SetFlags(ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocPageValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(static_cast<int64>(Alloc->GetPage()));
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocPageValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsHex64>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Alloc Count Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::CountColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("CountColumnName", "Count"));
		Column.SetTitleName(LOCTEXT("CountColumnTitle", "Allocation Count"));
		Column.SetDescription(LOCTEXT("CountColumnDesc", "Number of allocations"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocCountValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(static_cast<int64>(1));
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocCountValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Size Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::SizeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("SizeColumnName", "Size"));
		Column.SetTitleName(LOCTEXT("SizeColumnTitle", "Size"));
		Column.SetDescription(LOCTEXT("SizeColumnDesc", "Size of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FMemAllocSizeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(static_cast<int64>(Alloc->GetSize()));
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocSizeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsMemory>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);

		TSharedRef<IFilterValueConverter> Converter = MakeShared<FMemoryFilterValueConverter>();
		Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// LLM Tag Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::TagColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("TagColumnName", "LLM Tag"));
		Column.SetTitleName(LOCTEXT("TagColumnTitle", "LLM Tag"));
		Column.SetDescription(LOCTEXT("TagColumnDesc", "LLM tag of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FMemAllocLlmTagValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						return FTableCellValue(Alloc->GetTag());
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FMemAllocLlmTagValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Function Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FMemAllocTableColumns::FunctionColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("FunctionColumnName", "Function"));
		Column.SetTitleName(LOCTEXT("FunctionColumnTitle", "Function (Callstack)"));
		Column.SetDescription(LOCTEXT("FunctionColumnDesc", "Resolved top function from the callstack of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(550.0f);

		Column.SetDataType(ETableCellDataType::Text);

		Column.SetIsDynamic(true);

		class FFunctionValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FMemAllocNode>())
				{
					const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
					const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
					if (Alloc)
					{
						static FString NotAvailable = TEXT("Unknown callstack");
						const TraceServices::FCallstack* Callstack = Alloc->GetCallstack();

						if (Callstack)
						{
							check(Callstack->Num() > 0);

							const TraceServices::FStackFrame* Frame = nullptr;
							const TraceServices::FStackFrame* FirstUnknownSymbol = nullptr;
							for (uint32 FrameIndex = 0; FrameIndex < Callstack->Num(); ++FrameIndex)
							{
								Frame = Callstack->Frame(FrameIndex);
								check(Frame != nullptr);

								if (!Frame->Symbol || !Frame->Symbol->Name)
								{
									if (FirstUnknownSymbol == nullptr)
									{
										FirstUnknownSymbol = Frame;
									}
									continue;
								}

								bool bIgnoreSymbol = false;

								// Ignore symbols by function prefix.
								FStringView IgnoreSymbolsByFunctionName[] =
								{
									TEXT("FMemory::"_SV),
									TEXT("FMallocWrapper::"_SV),
									TEXT("FMallocPoisonProxy::"_SV),
									TEXT("Malloc"_SV),
									TEXT("Realloc"_SV),
								};
								for (uint32 StringIndex = 0; StringIndex < UE_ARRAY_COUNT(IgnoreSymbolsByFunctionName); ++StringIndex)
								{
									if (FCString::Strnicmp(Frame->Symbol->Name, IgnoreSymbolsByFunctionName[StringIndex].GetData(), IgnoreSymbolsByFunctionName[StringIndex].Len()) == 0)
									{
										bIgnoreSymbol = true;
										break;
									}
								}

#if 1 // TODO: check perf impact
								if (!bIgnoreSymbol && Frame->Symbol->File)
								{
									// Ignore symbols by file, specified as RegexPattern strings.
									FStringView IgnoreSymbolsByFilePath[] =
									{
										TEXT(".*/Containers/.*"_SV),
									};
									for (uint32 StringIndex = 0; StringIndex < UE_ARRAY_COUNT(IgnoreSymbolsByFilePath); ++StringIndex)
									{
										const FString Pattern(IgnoreSymbolsByFilePath[StringIndex]);
										const FRegexPattern RegexPattern(Pattern);
										FString File(Frame->Symbol->File);
										File.ReplaceCharInline(TEXT('\\'), TEXT('/'), ESearchCase::CaseSensitive);
										FRegexMatcher RegexMatcher(RegexPattern, File);
										if (RegexMatcher.FindNext())
										{
											bIgnoreSymbol = true;
											break;
										}
									}
								}
#endif

								if (!bIgnoreSymbol)
								{
									break;
								}
							}
							if (!Frame)
							{
								check(FirstUnknownSymbol != nullptr);
								Frame = FirstUnknownSymbol;
							}

							TStringBuilder<1024> Str;
							FormatStackFrame(*Frame, Str, EStackFrameFormatFlags::Module);
							return FTableCellValue(FText::FromString(FString(Str)));
						}

						return FTableCellValue(FText::FromString(NotAvailable));
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FFunctionValueGetter>();
		Column.SetValueGetter(Getter);

		class FunctionValueFormatter : public FTextValueFormatter
		{
		public:
			virtual TSharedPtr<IToolTip> GetCustomTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);

				return SNew(SToolTip)
						.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FTableCellValueFormatter::GetTooltipVisibility)))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(2.0f)
							[
								SNew(STextBlock)
								.Text(&MemAllocNode, &FMemAllocNode::GetFullCallstack)
							]
						];
			}

			virtual FText FormatValueForTooltip(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(Node);
				return MemAllocNode.GetFullCallstack();
			}
		};

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FunctionValueFormatter>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByTextValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
