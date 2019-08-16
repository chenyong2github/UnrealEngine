// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

enum ETableColumnType
{
	TableColumnType_Invalid,
	TableColumnType_Bool,
	TableColumnType_Int,
	TableColumnType_Float,
	TableColumnType_Double,
	TableColumnType_CString,
};

class ITableLayout
{
public:
	virtual ~ITableLayout() = default;
	virtual uint8 GetColumnCount() const = 0;
	virtual const TCHAR* GetColumnName(uint8 ColumnIndex) const = 0;
	virtual ETableColumnType GetColumnType(uint8 ColumnIndex) const = 0;
};

class IUntypedTableReader
{
public:
	virtual ~IUntypedTableReader() = default;
	virtual bool IsValid() const = 0;
	virtual void NextRow() = 0;
	virtual void SetRowIndex(uint64 RowIndex) = 0;
	virtual bool GetValueBool(uint8 ColumnIndex) const = 0;
	virtual int64 GetValueInt(uint8 ColumnIndex) const = 0;
	virtual float GetValueFloat(uint8 ColumnIndex) const = 0;
	virtual double GetValueDouble(uint8 ColumnIndex) const = 0;
	virtual const TCHAR* GetValueCString(uint8 ColumnIndex) const = 0;
};

template<typename RowType>
class ITableReader
	: public IUntypedTableReader
{
public:
	virtual const RowType* GetCurrentRow() const = 0;
};

class IUntypedTable
{
public:
	virtual ~IUntypedTable() = default;
	virtual const ITableLayout& GetLayout() const = 0;
	virtual uint64 GetRowCount() const = 0;
	virtual IUntypedTableReader* CreateReader() const = 0;
};

template<typename RowType>
class ITable
	: public IUntypedTable
{
public:
	virtual ~ITable() = default;
	virtual ITableReader<RowType>* CreateReader() const = 0;
};

}