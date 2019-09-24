// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	return Columns.IndexOfByPredicate([&ColumnId](const TSharedPtr<FTableColumn>& ColumnPtr) -> bool { return ColumnPtr->GetId() == ColumnId; });
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

void FTable::SetColumns(const TArray<TSharedPtr<Insights::FTableColumn>>& InColumns)
{
	Columns.Reset(InColumns.Num());
	ColumnIdToPtrMapping.Reset();
	for (TSharedPtr<Insights::FTableColumn> ColumnPtr : InColumns)
	{
		AddColumn(ColumnPtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::AddColumn(TSharedPtr<FTableColumn> ColumnPtr)
{
	ColumnPtr->SetParentTable(SharedThis(this));
	Columns.Add(ColumnPtr);
	ColumnIdToPtrMapping.Add(ColumnPtr->GetId(), ColumnPtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::CreateHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName)
{
	const FName HierarchyColumnId(TEXT("_Hierarchy"));

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(HierarchyColumnId));
	FTableColumn& Column = *ColumnPtr;
	
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

	TSharedPtr<ITableCellValueGetter> GetterPtr = MakeShareable(new FDisplayNameValueGetter());
	Column.SetValueGetter(GetterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueFormatter> FormatterPtr = MakeShareable(new FTextValueFormatter());
	Column.SetValueFormatter(FormatterPtr.ToSharedRef());

	TSharedPtr<ITableCellValueSorter> SorterPtr = MakeShareable(new FSorterByName(ColumnPtr.ToSharedRef()));
	Column.SetValueSorter(SorterPtr);

	AddColumn(ColumnPtr);
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

		TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn(FName(ColumnName)));
		FTableColumn& Column = *ColumnPtr;

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
			FormatterPtr = MakeShareable(new FBoolValueFormatterAsTrueFalse());
			SorterPtr = MakeShareable(new FSorterByBoolValue(ColumnPtr.ToSharedRef()));
			break;

		case Trace::TableColumnType_Int:
			Column.SetDataType(ETableCellDataType::Int64);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			//TODO: if (Hint == AsMemory)
			//{
			//	FormatterPtr = MakeShareable(new FInt64ValueFormatterAsMemory());
			//}
			//else // AsNumber
			FormatterPtr = MakeShareable(new FInt64ValueFormatterAsNumber());
			SorterPtr = MakeShareable(new FSorterByInt64Value(ColumnPtr.ToSharedRef()));
			break;

		case Trace::TableColumnType_Float:
			Column.SetDataType(ETableCellDataType::Float);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			//TODO: if (Hint == AsTimeMs)
			//else // if (Hint == AsTimeAuto)
			FormatterPtr = MakeShareable(new FFloatValueFormatterAsTimeAuto());
			SorterPtr = MakeShareable(new FSorterByFloatValue(ColumnPtr.ToSharedRef()));
			break;

		case Trace::TableColumnType_Double:
			Column.SetDataType(ETableCellDataType::Double);
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 80.0f;
			//TODO: if (Hint == AsTimeMs)
			//else // if (Hint == AsTimeAuto)
			FormatterPtr = MakeShareable(new FDoubleValueFormatterAsTimeAuto());
			SorterPtr = MakeShareable(new FSorterByDoubleValue(ColumnPtr.ToSharedRef()));
			break;

		case Trace::TableColumnType_CString:
			Column.SetDataType(ETableCellDataType::CString);
			HorizontalAlignment = HAlign_Left;
			InitialColumnWidth = FMath::Max(120.0f, 6.0f * ColumnNameStr.Len());
			FormatterPtr = MakeShareable(new FCStringValueFormatterAsText());
			SorterPtr = MakeShareable(new FSorterByCStringValue(ColumnPtr.ToSharedRef()));
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

		Column.SetValueGetter(MakeShareable(new FTableTreeNodeValueGetter(Column.GetDataType())));

		if (FormatterPtr.IsValid())
		{
			Column.SetValueFormatter(FormatterPtr.ToSharedRef());
		}

		Column.SetValueSorter(SorterPtr);

		AddColumn(ColumnPtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
