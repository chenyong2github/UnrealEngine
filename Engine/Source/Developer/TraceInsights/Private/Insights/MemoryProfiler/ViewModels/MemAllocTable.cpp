// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocTable.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocTable"

namespace Insights
{

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
		int32 HierarchyColumnIndex = -1;
		const TCHAR* HierarchyColumnName = nullptr;
		AddHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

		const TSharedRef<FTableColumn>& ColumnRef = GetColumns()[0];
		ColumnRef->SetInitialWidth(200.0f);
		ColumnRef->SetShortName(LOCTEXT("AllocationColumnName", "Hierarchy"));
		ColumnRef->SetTitleName(LOCTEXT("AllocationColumnTitle", "Allocation Hierarchy"));
		ColumnRef->SetDescription(LOCTEXT("AllocationColumnDesc", "Hierarchy of the allocation's tree"));
	}
	//////////////////////////////////////////////////
	// Start Time
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocStartTime"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(0);

		Column.SetShortName(LOCTEXT("StartTimeColumnName", "Start Time"));
		Column.SetTitleName(LOCTEXT("StartTimeColumnTitle", "Start Time"));
		Column.SetDescription(LOCTEXT("StartTimeColumnDesc", "The time when the allocation was allocated."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

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

		Column.SetAggregation(ETableColumnAggregation::Min);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// End Time
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocEndTime"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(1);

		Column.SetShortName(LOCTEXT("EndTimeColumnName", "End Time"));
		Column.SetTitleName(LOCTEXT("EndTimeColumnTitle", "End Time"));
		Column.SetDescription(LOCTEXT("EndTimeColumnDesc", "The time when the allocation was freed."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

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
						return FTableCellValue(Alloc->GetEndTime());
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

		Column.SetAggregation(ETableColumnAggregation::Max);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Duration
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocDuration"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(2);

		Column.SetShortName(LOCTEXT("DurationColumnName", "Duration"));
		Column.SetTitleName(LOCTEXT("DurationColumnTitle", "Duration"));
		Column.SetDescription(LOCTEXT("DurationColumnDesc", "The duration of the allocation's life."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

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
						return FTableCellValue(Alloc->GetDuration());
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

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Address Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocAddress"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(3);

		Column.SetShortName(LOCTEXT("AddressColumnName", "Address"));
		Column.SetTitleName(LOCTEXT("AddressColumnTitle", "Address"));
		Column.SetDescription(LOCTEXT("AddressColumnDesc", "Address of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

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
	// Size Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocSize"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(4);

		Column.SetShortName(LOCTEXT("SizeColumnName", "Size"));
		Column.SetTitleName(LOCTEXT("SizeColumnTitle", "Size"));
		Column.SetDescription(LOCTEXT("SizeColumnDesc", "Size of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
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

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// LLM Tag Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocLlmTag"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(5);

		Column.SetShortName(LOCTEXT("LlmTagColumnName", "LLM Tag"));
		Column.SetTitleName(LOCTEXT("LlmTagColumnTitle", "LLM Tag"));
		Column.SetDescription(LOCTEXT("LlmTagColumnDesc", "LLM tag of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(100.0f);

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
	// Backtrace Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(TEXT("AllocBacktrace"));
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(6);

		Column.SetShortName(LOCTEXT("BacktraceColumnName", "Backtrace"));
		Column.SetTitleName(LOCTEXT("BacktraceColumnTitle", "Backtrace"));
		Column.SetDescription(LOCTEXT("BacktraceColumnDesc", "Backtrace of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		Column.SetIsDynamic(true);

		class FBacktraceValueGetter : public FTableCellValueGetter
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
						static const TCHAR* DisplayStrings[] = {
							TEXT("Pending..."),
							TEXT("Not found"),
							TEXT("N/A"),
						};

						const TCHAR* Value = DisplayStrings[2]; // not available

						const TraceServices::FCallstack* Callstack = Alloc->GetCallstack();

						if (Callstack)
						{
							check(Callstack->Num() > 0);
							const TraceServices::FStackFrame* Frame = Callstack->Frame(FMath::Min(2u, Callstack->Num() - 1));
							check(Frame != nullptr);

							const TraceServices::QueryResult Result = Frame->Symbol->Result.load(std::memory_order_acquire);
							switch (Result)
							{
							case TraceServices::QueryResult::QR_NotLoaded:
								Value = DisplayStrings[0]; // pending
								break;
							case TraceServices::QueryResult::QR_NotFound:
								Value = DisplayStrings[1]; // not found
								break;
							case TraceServices::QueryResult::QR_OK:
								Value = Frame->Symbol->Name;
								break;
							}
						}

						return FTableCellValue(Value);
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FBacktraceValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
