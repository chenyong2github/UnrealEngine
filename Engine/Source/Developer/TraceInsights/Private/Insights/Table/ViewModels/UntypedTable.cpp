// Copyright Epic Games, Inc. All Rights Reserved.

#include "UntypedTable.h"
#include "TraceServices/Containers/Tables.h"

// Insights
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights::FUntypedTable"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FUntypedTableTreeNodeValueGetter
////////////////////////////////////////////////////////////////////////////////////////////////////

class FUntypedTableTreeNodeValueGetter : public FTableCellValueGetter
{
public:
	FUntypedTableTreeNodeValueGetter(ETableCellDataType InDataType) : FTableCellValueGetter(), DataType(InDataType) {}

	virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
	{
		ensure(Node.GetTypeName() == FTableTreeNode::TypeName);
		const FTableTreeNode& TableTreeNode = static_cast<const FTableTreeNode&>(Node);

		if (!Node.IsGroup()) // Table Row Node
		{
			const TSharedPtr<FTable> TablePtr = Column.GetParentTable().Pin();
			const TSharedPtr<FUntypedTable> UntypedTablePtr = StaticCastSharedPtr<FUntypedTable>(TablePtr);
			if (UntypedTablePtr.IsValid())
			{
				TSharedPtr<Trace::IUntypedTableReader> Reader = UntypedTablePtr->GetTableReader();
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
// FUntypedTable
////////////////////////////////////////////////////////////////////////////////////////////////////

FUntypedTable::FUntypedTable()
	: SourceTable()
	, TableReader()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FUntypedTable::~FUntypedTable()
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUntypedTable::Reset()
{
	SourceTable.Reset();
	TableReader.Reset();

	FTable::Reset();
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

bool FUntypedTable::UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable)
{
	bool bTableLayoutChanged;

	if (InSourceTable.IsValid())
	{
		bTableLayoutChanged = !SourceTable.IsValid() || !AreTableLayoutsEqual(InSourceTable->GetLayout(), SourceTable->GetLayout());
		SourceTable = InSourceTable;
		TableReader = MakeShareable(SourceTable->CreateReader());
	}
	else
	{
		bTableLayoutChanged = SourceTable.IsValid();
		SourceTable.Reset();
		TableReader.Reset();
	}

	if (bTableLayoutChanged)
	{
		ResetColumns();
		if (SourceTable.IsValid())
		{
			CreateColumns(SourceTable->GetLayout());
		}
	}

	return bTableLayoutChanged;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUntypedTable::CreateColumns(const Trace::ITableLayout& TableLayout)
{
	ensure(GetColumnCount() == 0);
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

	AddHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

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

		Column.SetValueGetter(MakeShared<FUntypedTableTreeNodeValueGetter>(Column.GetDataType()));

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
