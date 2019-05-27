// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "TraceServices/AnalysisService.h"
#include "Templates/Function.h"
#include "Common/PagedArray.h"
#include "Common/SlabAllocator.h"

#define UE_TRACE_TABLE_LAYOUT_BEGIN(Name, InRowType) \
	class Name \
		: public ::Trace::TTableLayoutBase<Name, InRowType> \
	{ \
	public: \
		typedef InRowType RowType; \
		virtual uint8 GetColumnCount() const override { return decltype(LastColumn)::Index; } \
		template<uint8 IndexValue> \
		constexpr ::Trace::ETableColumnType GetColumnTypeInternal() const { return TableColumnType_Invalid; } \
		template<uint8 IndexValue> \
		constexpr const TCHAR* GetColumnNameInternal() const { return nullptr; } \
		template<uint8 IndexValue> \
		::Trace::FColumnValueContainer GetColumnValueInternal(const RowType& Row) const { return 0; } \
		::Trace::TColumnDeclaration<0, RowType

#if PLATFORM_WINDOWS

#define UE_TRACE_TABLE_COLUMN(Name, DisplayName) \
		> PREPROCESSOR_JOIN(__Column__, __LINE__); \
		template<> \
		constexpr ::Trace::ETableColumnType GetColumnTypeInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>() const { return ::Trace::GetColumnTypeFromNativeType<decltype(GetMemberTypeHelper(&RowType::Name))>(); } \
		template<> \
		constexpr const TCHAR* GetColumnNameInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>() const { return DisplayName; } \
		template<> \
		::Trace::FColumnValueContainer GetColumnValueInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>(const RowType& Row) const { return Row.Name; } \
		::Trace::TColumnDeclaration<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index + 1, RowType
		
#define UE_TRACE_TABLE_PROJECTED_COLUMN(ColumnType, DisplayName, ProjectionFunc) \
		> PREPROCESSOR_JOIN(__Column__, __LINE__); \
		TFunction<FNativeTypeFromColumnType<::Trace::ColumnType>::Type(const RowType&)> PREPROCESSOR_JOIN(__ColumnProjector__, __LINE__) = ProjectionFunc; \
		template<> \
		constexpr ::Trace::ETableColumnType GetColumnTypeInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>() const { return ::Trace::ColumnType; } \
		template<> \
		constexpr const TCHAR* GetColumnNameInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>() const { return DisplayName; } \
		template<> \
		::Trace::FColumnValueContainer GetColumnValueInternal<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index>(const RowType& Row) const { return PREPROCESSOR_JOIN(__ColumnProjector__, __LINE__)(Row); } \
		::Trace::TColumnDeclaration<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index + 1, RowType

#else

#define UE_TRACE_TABLE_COLUMN(Name, DisplayName) \
		> PREPROCESSOR_JOIN(__Column__, __LINE__); \
		::Trace::TColumnDeclaration<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index + 1, RowType

#define UE_TRACE_TABLE_PROJECTED_COLUMN(ColumnType, DisplayName, ProjectionFunc) \
		> PREPROCESSOR_JOIN(__Column__, __LINE__); \
		::Trace::TColumnDeclaration<decltype(PREPROCESSOR_JOIN(__Column__, __LINE__))::Index + 1, RowType

#endif

#define UE_TRACE_TABLE_LAYOUT_END() \
		> LastColumn;\
	};

namespace Trace
{

template<typename Type>
static constexpr ETableColumnType GetColumnTypeFromNativeType();

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<bool>()
{
	return TableColumnType_Bool;
}

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<int8>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<uint8>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<int16>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<uint16>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<int32>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<uint32>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<int64>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<uint64>()
{
	return TableColumnType_Int;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<float>()
{
	return TableColumnType_Float;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<double>()
{
	return TableColumnType_Double;
};

template<>
constexpr ETableColumnType GetColumnTypeFromNativeType<const TCHAR*>()
{
	return TableColumnType_CString;
}

template<ETableColumnType ColumnType>
struct FNativeTypeFromColumnType
{
	
};

template<>
struct FNativeTypeFromColumnType<TableColumnType_Bool>
{
	using Type = bool;
};

template<>
struct FNativeTypeFromColumnType<TableColumnType_Int>
{
	using Type = int64;
};

template<>
struct FNativeTypeFromColumnType<TableColumnType_Float>
{
	using Type = float;
};

template<>
struct FNativeTypeFromColumnType<TableColumnType_Double>
{
	using Type = double;
};

template<>
struct FNativeTypeFromColumnType<TableColumnType_CString>
{
	using Type = const TCHAR*;
};

template<typename MemberType, typename ObjectType>
MemberType GetMemberTypeHelper(MemberType ObjectType::*);

struct FColumnValueContainer
{
	FColumnValueContainer(bool Value)
	{
		BoolValue = Value;
	}

	FColumnValueContainer(int8 Value)
	{
		IntValue = Value;
	}

	FColumnValueContainer(uint8 Value)
	{
		IntValue = Value;
	}

	FColumnValueContainer(int32 Value)
	{
		IntValue = Value;
	}

	FColumnValueContainer(uint32 Value)
	{
		IntValue = Value;
	}

	FColumnValueContainer(int64 Value)
	{
		IntValue = Value;
	}

	FColumnValueContainer(uint64 Value)
	{
		IntValue = int64(Value);
	}

	FColumnValueContainer(float Value)
	{
		FloatValue = Value;
	}

	FColumnValueContainer(double Value)
	{
		DoubleValue = Value;
	}

	FColumnValueContainer(const TCHAR* Value)
	{
		CStringValue = Value;
	}

	union
	{
		bool BoolValue;
		int64 IntValue;
		float FloatValue;
		double DoubleValue;
		const TCHAR* CStringValue;
	};
};

template<uint8 IndexValue, typename RowType>
struct TColumnDeclaration
{
	enum
	{
		Index = IndexValue
	};

	static constexpr ETableColumnType GetType() { return ::Trace::TableColumnType_Invalid; }
	static constexpr const TCHAR* GetName() { return nullptr; }
	static const void* GetValue(const RowType& Row) { return nullptr; }
};

template<typename LayoutType>
class TTableReader
	: public ITableReader<typename LayoutType::RowType>
{
public:
	TTableReader(const LayoutType& InLayout, const TPagedArray<typename LayoutType::RowType>& InRows)
		: Layout(InLayout)
		, Iterator(InRows.GetIteratorFromItem(0))
	{
		CurrentRow = Iterator.GetCurrentItem();
	}

	virtual bool IsValid() const override
	{
		return CurrentRow != nullptr;
	}

	virtual void NextRow() override
	{
		CurrentRow = Iterator.NextItem();
	}

	virtual void SetRowIndex(uint64 RowIndex) override
	{
		CurrentRow = Iterator.SetPosition(RowIndex);
	}

	virtual const typename LayoutType::RowType* GetCurrentRow() const override
	{
		return CurrentRow;
	}

	virtual bool GetValueBool(uint8 ColumnIndex) const override
	{
		if (!CurrentRow)
		{
			return false;
		}
		ETableColumnType ColumnType = Layout.GetColumnTypeConstExpr(ColumnIndex);
		switch (ColumnType)
		{
		case TableColumnType_Bool:
			return Layout.GetColumnValue(*CurrentRow, ColumnIndex).BoolValue;
		case TableColumnType_Int:
			return static_cast<bool>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).IntValue);
		case TableColumnType_Float:
			return static_cast<bool>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).FloatValue);
		case TableColumnType_Double:
			return static_cast<bool>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).DoubleValue);
		}
		return false;
	}

	virtual int64 GetValueInt(uint8 ColumnIndex) const override
	{
		if (!CurrentRow)
		{
			return 0;
		}
		ETableColumnType ColumnType = Layout.GetColumnTypeConstExpr(ColumnIndex);
		switch (ColumnType)
		{
		case TableColumnType_Bool:
			return static_cast<int64>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).BoolValue);
		case TableColumnType_Int:
			return Layout.GetColumnValue(*CurrentRow, ColumnIndex).IntValue;
		case TableColumnType_Float:
			return static_cast<int64>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).FloatValue);
		case TableColumnType_Double:
			return static_cast<int64>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).DoubleValue);
		}
		return 0;
	}

	virtual float GetValueFloat(uint8 ColumnIndex) const override
	{
		if (!CurrentRow)
		{
			return 0.0;
		}
		ETableColumnType ColumnType = Layout.GetColumnTypeConstExpr(ColumnIndex);
		switch (ColumnType)
		{
		case TableColumnType_Bool:
			return static_cast<float>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).BoolValue);
		case TableColumnType_Int:
			return static_cast<float>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).IntValue);
		case TableColumnType_Float:
			return Layout.GetColumnValue(*CurrentRow, ColumnIndex).FloatValue;
		case TableColumnType_Double:
			return static_cast<double>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).DoubleValue);
		}
		return 0.0;
	}

	virtual double GetValueDouble(uint8 ColumnIndex) const override
	{
		if (!CurrentRow)
		{
			return 0.0;
		}
		ETableColumnType ColumnType = Layout.GetColumnTypeConstExpr(ColumnIndex);
		switch (ColumnType)
		{
		case TableColumnType_Bool:
			return static_cast<double>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).BoolValue);
		case TableColumnType_Int:
			return static_cast<double>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).IntValue);
		case TableColumnType_Float:
			return static_cast<double>(Layout.GetColumnValue(*CurrentRow, ColumnIndex).FloatValue);
		case TableColumnType_Double:
			return Layout.GetColumnValue(*CurrentRow, ColumnIndex).DoubleValue;
		}
		return 0.0;
	}

	virtual const TCHAR* GetValueCString(uint8 ColumnIndex) const override
	{
		if (!CurrentRow)
		{
			return nullptr;
		}
		ETableColumnType ColumnType = Layout.GetColumnTypeConstExpr(ColumnIndex);
		if (ColumnType == TableColumnType_CString)
		{
			return Layout.GetColumnValue(*CurrentRow, ColumnIndex).CStringValue;
		}
		return nullptr;
	}

private:
	const LayoutType& Layout;
	typename TPagedArray<typename LayoutType::RowType>::FIterator Iterator;
	const typename LayoutType::RowType* CurrentRow;
};

template<typename LayoutType, typename RowType>
class TTableLayoutBase
	: public ITableLayout
{
public:
	constexpr ETableColumnType GetColumnTypeConstExpr(uint8 ColumnIndex) const
	{
		const LayoutType* This = static_cast<const LayoutType*>(this);
		switch (ColumnIndex)
		{
		case 0: return This->template GetColumnTypeInternal<0>();
		case 1: return This->template GetColumnTypeInternal<1>();
		case 2: return This->template GetColumnTypeInternal<2>();
		case 3: return This->template GetColumnTypeInternal<3>();
		case 4: return This->template GetColumnTypeInternal<4>();
		case 5: return This->template GetColumnTypeInternal<5>();
		case 6: return This->template GetColumnTypeInternal<6>();
		case 7: return This->template GetColumnTypeInternal<7>();
		case 8: return This->template GetColumnTypeInternal<8>();
		case 9: return This->template GetColumnTypeInternal<9>();
		case 10: return This->template GetColumnTypeInternal<10>();
		case 11: return This->template GetColumnTypeInternal<11>();
		case 12: return This->template GetColumnTypeInternal<12>();
		case 13: return This->template GetColumnTypeInternal<13>();
		case 14: return This->template GetColumnTypeInternal<14>();
		case 15: return This->template GetColumnTypeInternal<15>();
		}
		return ::Trace::TableColumnType_Invalid;
	}

	virtual ETableColumnType GetColumnType(uint8 ColumnIndex) const override
	{
		return GetColumnTypeConstExpr(ColumnIndex);
	}

	constexpr const TCHAR* GetColumnNameConstExpr(uint8 ColumnIndex) const
	{
		const LayoutType* This = static_cast<const LayoutType*>(this);
		switch (ColumnIndex)
		{
		case 0: return This->template GetColumnNameInternal<0>();
		case 1: return This->template GetColumnNameInternal<1>();
		case 2: return This->template GetColumnNameInternal<2>();
		case 3: return This->template GetColumnNameInternal<3>();
		case 4: return This->template GetColumnNameInternal<4>();
		case 5: return This->template GetColumnNameInternal<5>();
		case 6: return This->template GetColumnNameInternal<6>();
		case 7: return This->template GetColumnNameInternal<7>();
		case 8: return This->template GetColumnNameInternal<8>();
		case 9: return This->template GetColumnNameInternal<9>();
		case 10: return This->template GetColumnNameInternal<10>();
		case 11: return This->template GetColumnNameInternal<11>();
		case 12: return This->template GetColumnNameInternal<12>();
		case 13: return This->template GetColumnNameInternal<13>();
		case 14: return This->template GetColumnNameInternal<14>();
		case 15: return This->template GetColumnNameInternal<15>();
		}
		return nullptr;
	}

	virtual const TCHAR* GetColumnName(uint8 ColumnIndex) const override
	{
		return GetColumnNameConstExpr(ColumnIndex);
	}

	FColumnValueContainer GetColumnValue(const RowType& Row, uint8 ColumnIndex) const
	{
		const LayoutType* This = static_cast<const LayoutType*>(this);
		switch (ColumnIndex)
		{
		case 0: return This->template GetColumnValueInternal<0>(Row);
		case 1: return This->template GetColumnValueInternal<1>(Row);
		case 2: return This->template GetColumnValueInternal<2>(Row);
		case 3: return This->template GetColumnValueInternal<3>(Row);
		case 4: return This->template GetColumnValueInternal<4>(Row);
		case 5: return This->template GetColumnValueInternal<5>(Row);
		case 6: return This->template GetColumnValueInternal<6>(Row);
		case 7: return This->template GetColumnValueInternal<7>(Row);
		case 8: return This->template GetColumnValueInternal<8>(Row);
		case 9: return This->template GetColumnValueInternal<9>(Row);
		case 10: return This->template GetColumnValueInternal<10>(Row);
		case 11: return This->template GetColumnValueInternal<11>(Row);
		case 12: return This->template GetColumnValueInternal<12>(Row);
		case 13: return This->template GetColumnValueInternal<13>(Row);
		case 14: return This->template GetColumnValueInternal<14>(Row);
		case 15: return This->template GetColumnValueInternal<15>(Row);
		}
		return nullptr;
	}
};

template<typename LayoutType>
class TTableView
	: public ITable<typename LayoutType::RowType>
{
public:
	TTableView(const TPagedArray<typename LayoutType::RowType>& InRows)
		: Rows(InRows)
	{
	}

	virtual const ITableLayout& GetLayout() const override
	{
		return Layout;
	}

	virtual uint64 GetRowCount() const override
	{
		return Rows.Num();
	}

	virtual ITableReader<typename LayoutType::RowType>* CreateReader() const override
	{
		return new TTableReader<LayoutType>(Layout, Rows);
	}

private:
	LayoutType Layout;
	const TPagedArray<typename LayoutType::RowType>& Rows;
};

template<typename LayoutType>
class TTable
	: public ITable<typename LayoutType::RowType>
{
public:
	TTable()
		: Allocator(2 << 20)
		, Rows(Allocator, 1024)
	{

	}

	virtual const ITableLayout& GetLayout() const override
	{
		return Layout;
	}

	virtual uint64 GetRowCount() const override
	{
		return Rows.Num();
	}

	virtual ITableReader<typename LayoutType::RowType>* CreateReader() const override
	{
		return new TTableReader<LayoutType>(Layout, Rows);
	}

	typename LayoutType::RowType& AddRow()
	{
		return Rows.PushBack();
	}

private:
	LayoutType Layout;
	FSlabAllocator Allocator;
	TPagedArray<typename LayoutType::RowType> Rows;
};

}