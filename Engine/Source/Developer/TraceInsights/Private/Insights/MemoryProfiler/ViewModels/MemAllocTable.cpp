// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocTable.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocTable"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FMemAllocTableColumns::StartTimeColumnId(TEXT("StartTime"));
const FName FMemAllocTableColumns::EndTimeColumnId(TEXT("EndTime"));
const FName FMemAllocTableColumns::DurationColumnId(TEXT("Duration"));
const FName FMemAllocTableColumns::AddressColumnId(TEXT("Address"));
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
		Column.SetTitleName(LOCTEXT("FunctionColumnTitle", "Function"));
		Column.SetDescription(LOCTEXT("FunctionColumnDesc", "Resolved top function from the callstack of allocation"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(550.0f);

		Column.SetDataType(ETableCellDataType::CString);

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
						static const TCHAR* NotAvailable = TEXT("N/A");
						const TCHAR* Value = NotAvailable;

						const TraceServices::FCallstack* Callstack = Alloc->GetCallstack();

						if (Callstack)
						{
							check(Callstack->Num() > 0);
							const TraceServices::FStackFrame* Frame = Callstack->Frame(FMath::Min(2u, Callstack->Num() - 1));
							check(Frame != nullptr);

							const TraceServices::QueryResult Result = Frame->Symbol->GetResult();
							if (Result == TraceServices::QueryResult::OK)
							{
								Value = Frame->Symbol->Name;
							}
							else
							{
								Value = TraceServices::QueryResultToString(Result);
							}
						}

						return FTableCellValue(Value);
					}
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FFunctionValueGetter>();
		Column.SetValueGetter(Getter);

		class FunctionValueFormatter : public FCStringValueFormatterAsText
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

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
