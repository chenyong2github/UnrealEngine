// Copyright Epic Games, Inc. All Rights Reserved.

#include "Table.h"
#include "TraceServices/Containers/Tables.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights_Table"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTableTreeNodeValueGetter
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTableTreeNodeValueGetter : public FTableCellValueGetter
{
public:
	FTableTreeNodeValueGetter(ETableCellDataType InDataType) : FTableCellValueGetter(), DataType(InDataType) {}

	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
	{
		ensure(Node.GetTypeName() == FTableTreeNode::TypeName);
		const FTableTreeNode& TableTreeNode = static_cast<const FTableTreeNode&>(Node);

		if (!Node.IsGroup()) // Table Row Node
		{
			const TSharedPtr<FTable> TablePtr = Column.GetParentTable().Pin();
			if (TablePtr.IsValid())
			{
				TSharedPtr<Trace::IUntypedTableReader> Reader = TablePtr->GetTableReader();
				if (Reader.IsValid() && TableTreeNode.GetRowId().HasValidIndex())
				{
					Reader->SetRowIndex(TableTreeNode.GetRowId().RowIndex);
					const int32 ColumnIndex = Column.GetIndex();
					switch (DataType)
					{
						case ETableCellDataType::Bool:    return TOptional<FTableCellValue>(Reader->GetValueBool(ColumnIndex));
						case ETableCellDataType::Int64:   return TOptional<FTableCellValue>(Reader->GetValueInt(ColumnIndex));
						case ETableCellDataType::Float:   return TOptional<FTableCellValue>(Reader->GetValueFloat(ColumnIndex));
						case ETableCellDataType::Double:  return TOptional<FTableCellValue>(Reader->GetValueDouble(ColumnIndex));
						case ETableCellDataType::CString: return TOptional<FTableCellValue>(Reader->GetValueCString(ColumnIndex));
					}
				}
			}
		}
		else // Aggregated Group Node
		{
			if (Column.GetAggregation() != ETableColumnAggregation::None)
			{
				const FTableCellValue* ValuePtr = TableTreeNode.FindAggregatedValue(Column.GetId());
				if (ValuePtr != nullptr)
				{
					ensure(ValuePtr->DataType == DataType);
					return TOptional<FTableCellValue>(*ValuePtr);
				}
			}
		}

		return TOptional<FTableCellValue>();
	}

private:
	ETableCellDataType DataType;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTable
////////////////////////////////////////////////////////////////////////////////////////////////////

FTable::FTable()
	: Name()
	, Description()
	, Columns()
	, ColumnIdToPtrMapping()
	, SourceTable()
	, TableReader()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTable::~FTable()
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::Reset()
{
	Columns.Reset();
	ColumnIdToPtrMapping.Reset();

	SourceTable.Reset();
	TableReader.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTable::GetColumnPositionIndex(const FName& ColumnId) const
{
	return Columns.IndexOfByPredicate([&ColumnId](const TSharedRef<FTableColumn>& ColumnRef) -> bool { return ColumnRef->GetId() == ColumnId; });
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::Init(TSharedPtr<Trace::IUntypedTable> InSourceTable)
{
	Reset();

	SourceTable = InSourceTable;

	if (SourceTable)
	{
		TableReader = MakeShareable(SourceTable->CreateReader());
		CreateColumnsFromTableLayout();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool AreTableLayoutsEqual(const Trace::ITableLayout& TableLayoutA, const Trace::ITableLayout& TableLayoutB)
{
	if (TableLayoutA.GetColumnCount() != TableLayoutB.GetColumnCount())
	{
		return false;
	}

	int32 ColumnCount = TableLayoutA.GetColumnCount();
	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		if (TableLayoutA.GetColumnType(ColumnIndex) != TableLayoutB.GetColumnType(ColumnIndex))
		{
			return false;
		}
		if (FCString::Strcmp(TableLayoutA.GetColumnName(ColumnIndex), TableLayoutB.GetColumnName(ColumnIndex)) != 0)
		{
			return false;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable)
{
	check(InSourceTable.IsValid() && SourceTable.IsValid());
	check(AreTableLayoutsEqual(InSourceTable->GetLayout(), SourceTable->GetLayout()));
	SourceTable = InSourceTable;
	TableReader = MakeShareable(SourceTable->CreateReader());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::SetColumns(const TArray<TSharedRef<Insights::FTableColumn>>& InColumns)
{
	Columns.Reset(InColumns.Num());
	ColumnIdToPtrMapping.Reset();
	for (TSharedRef<Insights::FTableColumn> ColumnRef : InColumns)
	{
		AddColumn(ColumnRef);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::AddColumn(TSharedRef<FTableColumn> ColumnRef)
{
	ColumnRef->SetParentTable(SharedThis(this));
	Columns.Add(ColumnRef);
	ColumnIdToPtrMapping.Add(ColumnRef->GetId(), ColumnRef);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::CreateHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName)
{
	const FName HierarchyColumnId(TEXT("_Hierarchy"));

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(HierarchyColumnId);
	FTableColumn& Column = *ColumnRef;

	Column.SetIndex(ColumnIndex);

	const FString ColumnNameStr = ColumnName ? FString::Printf(TEXT("Hierarchy (%s)"), ColumnName) : TEXT("Hierarchy");
	const FText ColumnNameText = FText::FromString(ColumnNameStr);

	Column.SetShortName(ColumnNameText);
	Column.SetTitleName(ColumnNameText);
	//TODO: Column.SetDescription(...);

	Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::IsHierarchy);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(90.0f);

	Column.SetDataType(ETableCellDataType::CString);

	TSharedRef<ITableCellValueGetter> Getter = MakeShared<FDisplayNameValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FTextValueFormatter>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByName>(ColumnRef);
	Column.SetValueSorter(Sorter);

	AddColumn(ColumnRef);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::CreateColumnsFromTableLayout()
{
	ensure(TableReader.IsValid());
	ensure(Columns.Num() == 0);

	const Trace::ITableLayout& TableLayout = SourceTable->GetLayout();
	const int32 ColumnCount = TableLayout.GetColumnCount();

	//////////////////////////////////////////////////
	// Hierarchy Column

	int32 HierarchyColumnIndex = -1;
	const TCHAR* HierarchyColumnName = nullptr;

	// Look for first string column.
	//for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	//{
	//	Trace::ETableColumnType ColumnType = TableLayout.GetColumnType(ColumnIndex);
	//	if (ColumnType == Trace::TableColumnType_CString)
	//	{
	//		HierarchyColumnIndex = ColumnIndex;
	//		HierarchyColumnName = TableLayout.GetColumnName(ColumnIndex);
	//		break;
	//	}
	//}

	CreateHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

	//////////////////////////////////////////////////

	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		Trace::ETableColumnType ColumnType = TableLayout.GetColumnType(ColumnIndex);
		const TCHAR* ColumnName = TableLayout.GetColumnName(ColumnIndex);

		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FName(ColumnName));
		FTableColumn& Column = *ColumnRef;

		const FString ColumnNameStr(ColumnName);
		const FText ColumnNameText = FText::FromString(ColumnNameStr);

		ETableColumnFlags ColumnFlags = ETableColumnFlags::CanBeFiltered | ETableColumnFlags::CanBeHidden;
		if (ColumnIndex != HierarchyColumnIndex)
		{
			ColumnFlags |= ETableColumnFlags::ShouldBeVisible;
		}

		EHorizontalAlignment HorizontalAlignment = HAlign_Left;
		float InitialColumnWidth = 60.0f;

		ETableColumnAggregation Aggregation = ETableColumnAggregation::None;

		TSharedPtr<ITableCellValueFormatter> FormatterPtr;
		TSharedPtr<ITableCellValueSorter> SorterPtr;

		switch (ColumnType)
		{
		case Trace::TableColumnType_Bool:
			Column.SetDataType(ETableCellDataType::Bool);
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 40.0f;
			//TODO: if (Hint == AsOnOff)
			//else // if (Hint == AsTrueFalse)
			FormatterPtr = MakeShared<FBoolValueFormatterAsTrueFalse>();
			SorterPtr = MakeShared<FSorterByBoolValue>(ColumnRef);
			break;

		case Trace::TableColumnType_Int:
			Column.SetDataType(ETableCellDataType::Int64);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			//TODO: if (Hint == AsMemory)
			//{
			//	FormatterPtr = MakeShared<FInt64ValueFormatterAsMemory>();
			//}
			//else // AsNumber
			FormatterPtr = MakeShared<FInt64ValueFormatterAsNumber>();
			SorterPtr = MakeShared<FSorterByInt64Value>(ColumnRef);
			break;

		case Trace::TableColumnType_Float:
			Column.SetDataType(ETableCellDataType::Float);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			//TODO: if (Hint == AsTimeMs)
			//else // if (Hint == AsTimeAuto)
			FormatterPtr = MakeShared<FFloatValueFormatterAsTimeAuto>();
			SorterPtr = MakeShared<FSorterByFloatValue>(ColumnRef);
			break;

		case Trace::TableColumnType_Double:
			Column.SetDataType(ETableCellDataType::Double);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 80.0f;
			//TODO: if (Hint == AsTimeMs)
			//else // if (Hint == AsTimeAuto)
			FormatterPtr = MakeShared<FDoubleValueFormatterAsTimeAuto>();
			SorterPtr = MakeShared<FSorterByDoubleValue>(ColumnRef);
			break;

		case Trace::TableColumnType_CString:
			Column.SetDataType(ETableCellDataType::CString);
			HorizontalAlignment = HAlign_Left;
			InitialColumnWidth = FMath::Max(120.0f, 6.0f * ColumnNameStr.Len());
			FormatterPtr = MakeShared<FCStringValueFormatterAsText>();
			SorterPtr = MakeShared<FSorterByCStringValue>(ColumnRef);
			break;
		}

		Column.SetIndex(ColumnIndex);

		Column.SetShortName(ColumnNameText);
		Column.SetTitleName(ColumnNameText);

		//TODO: Column.SetDescription(...);

		Column.SetFlags(ColumnFlags);

		Column.SetHorizontalAlignment(HorizontalAlignment);
		Column.SetInitialWidth(InitialColumnWidth);

		Column.SetAggregation(Aggregation);

		Column.SetValueGetter(MakeShared<FTableTreeNodeValueGetter>(Column.GetDataType()));

		if (FormatterPtr.IsValid())
		{
			Column.SetValueFormatter(FormatterPtr.ToSharedRef());
		}

		Column.SetValueSorter(SorterPtr);

		AddColumn(ColumnRef);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
