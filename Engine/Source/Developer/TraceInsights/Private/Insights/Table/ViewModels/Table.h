// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Trace
{
	class IUntypedTable;
	class IUntypedTableReader;
}

namespace Insights
{

class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTableRowId
{
	static constexpr int32 InvalidRowIndex = -1;

	FTableRowId(int32 InRowIndex) : RowIndex(InRowIndex), Flags(0) {}

	bool HasValidIndex() const { return RowIndex >= 0; }

	union
	{
		struct
		{
			int32 RowIndex;
			uint32 Flags;
		};

		void* Data;
	};
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Table View Model.
 * View model class for the STableListView and STableTreeView widgets.
 */
class FTable : public TSharedFromThis<FTable>
{
public:
	FTable();
	virtual ~FTable();

	const FName& GetName() const { return Name; }
	FText GetDisplayName() const { return FText::FromName(Name); }
	const FText& GetDescription() const { return Description; }

	TSharedPtr<Trace::IUntypedTable> GetSourceTable() const { return SourceTable; }
	TSharedPtr<Trace::IUntypedTableReader> GetTableReader() const { return TableReader; }

	void Reset();
	void Init(TSharedPtr<Trace::IUntypedTable> InSourceTable);
	void UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable);

	bool IsValid() const { return Columns.Num() > 0; }

	const TArray<TSharedPtr<FTableColumn>>& GetColumns() const { return Columns; }

	TSharedPtr<FTableColumn> FindColumnChecked(const FName& ColumnId) const
	{
		return ColumnIdToPtrMapping.FindChecked(ColumnId);
	}

	TSharedPtr<FTableColumn> FindColumn(const FName& ColumnId) const
	{
		const TSharedPtr<FTableColumn>* const ColumnPtrPtr = ColumnIdToPtrMapping.Find(ColumnId);
		return (ColumnPtrPtr != nullptr) ? *ColumnPtrPtr : nullptr;
	}

private:
	void CreateHierarchyColumn(const TCHAR* ColumnName);
	void AddColumn(TSharedPtr<FTableColumn> ColumnPtr);
	void CreateColumns();

private:
	FName Name;
	FText Description;

	TSharedPtr<Trace::IUntypedTable> SourceTable;
	TSharedPtr<Trace::IUntypedTableReader> TableReader;

	/** All available columns. */
	TArray<TSharedPtr<FTableColumn>> Columns;

	/** The visible columns. */
	TArray<TSharedPtr<FTableColumn>> VisibleColumns;

	/** Mapping between column Ids and FTableColumn shared pointers. */
	TMap<FName, TSharedPtr<FTableColumn>> ColumnIdToPtrMapping;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
