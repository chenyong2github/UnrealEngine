// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Misc/EnumClassFlags.h"

namespace Insights
{

class FTable;
class FTableColumn;
struct FTableRowId;

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETableColumnFlags : uint32
{
	None = 0,

	ShouldBeVisible = (1 << 0),
	CanBeHidden     = (1 << 1),
	CanBeSorted     = (1 << 2),
	CanBeFiltered   = (1 << 3),
	IsHierarchy     = (1 << 4),
};
ENUM_CLASS_FLAGS(ETableColumnFlags);

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETableCellDataType : uint32
{
	Unknown,

	Bool,
	Int64,
	Float,
	Double,
	CString,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTableCellValue
{
	FTableCellValue() = default;
	FTableCellValue(bool Value) : Bool(Value) {}
	FTableCellValue(int64 Value) : Int64(Value) {}
	FTableCellValue(float Value) : Float(Value) {}
	FTableCellValue(double Value) : Double(Value) {}
	FTableCellValue(const TCHAR* Value) : CString(Value) {}

	union
	{
		bool Bool;
		int64 Int64;
		float Float;
		double Double;
		const TCHAR* CString;
	};
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTableCellValueFormatter
{
public:
	FTableCellValueFormatter() {}
	virtual ~FTableCellValueFormatter() {}

	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) { return FText::GetEmpty(); }
	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) { return FormatValue(InValue); }

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) { return FText::GetEmpty(); }
	virtual FText FormatValueForTooltip(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) { return FormatValue(Table, Column, RowId); }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETableColumnAggregation : uint32
{
	None = 0,
	Sum,
	//Min,
	//Max,
	//Average,
	//Median,
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Table Column View Model.
 * Holds information about a column in STableListView or STableTreeView widgets.
 */
class FTableColumn
{
public:
	typedef TFunction<FText(const FTableCellValue& Value)> FGetCellValueAsTextFunction;
	typedef TFunction<FText(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId)> FGetValueAsTextFunction;

public:
	/** No default constructor. */
	FTableColumn() = delete;

	/** Initialization constructor. */
	FTableColumn
	(
		int32 InOrder,
		int32 InIndex,
		const FName InId,
		FText InShortName,
		FText InTitleName,
		FText InDescription,
		const ETableColumnFlags InFlags,
		const ETableCellDataType InDataType,
		const ETableColumnAggregation InAggregation,
		const EHorizontalAlignment InHorizontalAlignment,
		const float InInitialWidth,
		const float InMinWidth,
		const float InMaxWidth,
		TSharedRef<FTableCellValueFormatter> InValueFormatter
	)
		: Order(InOrder)
		, Index(InIndex)
		, Id(InId)
		, ShortName(MoveTemp(InShortName))
		, TitleName(MoveTemp(InTitleName))
		, Description(MoveTemp(InDescription))
		, bIsVisible(false)
		, Flags(InFlags)
		, DataType(InDataType)
		, Aggregation(InAggregation)
		, HorizontalAlignment(InHorizontalAlignment)
		, InitialWidth(InInitialWidth)
		, MinWidth(InMinWidth)
		, MaxWidth(InMaxWidth)
		, ValueFormatter(InValueFormatter)
		, ParentTable(nullptr)
	{
	}

	int32 GetOrder() const { return Order; }
	int32 GetIndex() const { return Index; }

	const FName& GetId() const { return Id; }

	const FText& GetShortName() const { return ShortName; }
	const FText& GetTitleName() const { return TitleName; }
	const FText& GetDescription() const { return Description; }

	void SetShortName(const FText& InShortName) { ShortName = InShortName; }
	void SetTitleName(const FText& InTitleName) { TitleName = InTitleName; }
	void SetDescription(const FText& InDescription) { Description = InDescription; }

	bool IsVisible() const { return bIsVisible; }
	void Show() { bIsVisible = true; OnVisibilityChanged(); }
	void Hide() { bIsVisible = false; OnVisibilityChanged(); }
	void ToggleVisibility() { bIsVisible = !bIsVisible; OnVisibilityChanged(); }
	void SetVisibilityFlag(bool bOnOff) { bIsVisible = bOnOff; OnVisibilityChanged(); }
	/*virtual*/ void OnVisibilityChanged() {}

	/** Whether this column should be initially visible. */
	bool ShouldBeVisible() const { return EnumHasAnyFlags(Flags, ETableColumnFlags::ShouldBeVisible); }

	/** Whether this column can be hidden. */
	bool CanBeHidden() const { return EnumHasAnyFlags(Flags, ETableColumnFlags::CanBeHidden); }

	/** Whether this column can be used for sorting. */
	bool CanBeSorted() const { return EnumHasAnyFlags(Flags, ETableColumnFlags::CanBeSorted); }

	/** Whether this column can be used for filtering displayed results. */
	bool CanBeFiltered() const { return EnumHasAnyFlags(Flags, ETableColumnFlags::CanBeFiltered); }

	/** Whether this column is the hierarcy (name) column, in a tree view. */
	bool IsHierarchy() const { return EnumHasAnyFlags(Flags, ETableColumnFlags::IsHierarchy); }

	ETableCellDataType GetDataType() const { return DataType; }
	ETableColumnAggregation GetAggregation() const { return Aggregation; }

	EHorizontalAlignment GetHorizontalAlignment() const { return HorizontalAlignment; }

	float GetInitialWidth() const { return InitialWidth; }
	float GetMinWidth() const { return MinWidth; }
	float GetMaxWidth() const { return MaxWidth; }
	//float GetWidth() const { return Width; }

	/** If MinWidth == MaxWidth, this column has fixed width and cannot be resized. */
	bool IsFixedWidth() const { return MinWidth == MaxWidth; }

	TSharedRef<FTableCellValueFormatter> GetValueFormatter() const { return ValueFormatter; }

	FText GetValueAsText(const FTableRowId& InRowId) const
	{
		return ValueFormatter->FormatValue(*ParentTable.Pin(), *this, InRowId);
	}

	FText GetValueAsTooltipText(const FTableRowId& InRowId) const
	{
		return ValueFormatter->FormatValueForTooltip(*ParentTable.Pin(), *this, InRowId);
	}

	TWeakPtr<FTable> GetParentTable() const { return ParentTable; }
	void SetParentTable(TWeakPtr<FTable> InParentTable) { ParentTable = InParentTable; }

private:
	/** Order value, to sort columns in the list/tree view. */
	int32 Order;

	/** Column index in source table. */
	int32 Index;

	/** Id of the column. */
	FName Id;

	/** Short name of the column, displayed in the column header. */
	FText ShortName;

	/** Title name of the column, displayed as title in the column tooltip. */
	FText TitleName;

	/** Long name of the column, displayed in the column tooltip. */
	FText Description;

	/** Is this column visible? */
	bool bIsVisible;

	/** Other on/off switches. */
	ETableColumnFlags Flags;

	/** Data type for this column. */
	ETableCellDataType DataType;

	/** Aggregation for values in this column, when grouped. */
	ETableColumnAggregation Aggregation; // TODO: make this an object (Aggregator)

	/** Horizontal alignment of the content in this column. */
	EHorizontalAlignment HorizontalAlignment;

	float InitialWidth; /**< Initial column width. */
	float MinWidth; /**< Minimum column width. */
	float MaxWidth; /**< Maximum column width. */
	//float Width; /**< Current column width. */

	/** Custom formater for the value displayed by this column. */
	TSharedRef<FTableCellValueFormatter> ValueFormatter;

	/* Parent table. Only one table instance can own this column. */
	TWeakPtr<FTable> ParentTable;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
