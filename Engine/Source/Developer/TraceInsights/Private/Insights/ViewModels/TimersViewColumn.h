// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class FTimerNode;

/// Holds information about a column in the timer view widget.
class FTimersViewColumn
{
	friend struct FTimersViewColumnFactory;

public:
	enum EFlags : uint32
	{
		None = 0,

		CanBeHidden = (1 << 0),
		CanBeSorted = (1 << 1),
		CanBeFiltered = (1 << 2),

		NameColumnFlags = CanBeSorted | CanBeFiltered,
		StatsColumnFlags = CanBeHidden | CanBeSorted | CanBeFiltered,
	};

	typedef TFunction<FText(const FTimersViewColumn& Column, const FTimerNode& TimerNode)> FGetFormattedValueFn;

public:
	/// Whether this column can be hidden.
	bool bCanBeHidden() const { return ((uint32)Flags & (uint32)EFlags::CanBeHidden) != 0; }

	/// Whether this column cab be used for sorting.
	bool bCanBeSorted() const { return ((uint32)Flags & (uint32)EFlags::CanBeSorted) != 0; }

	/// Where this column can be used to filtering displayed results.
	bool bCanBeFiltered() const { return ((uint32)Flags & (uint32)EFlags::CanBeFiltered) != 0; }

	/// If MinColumnWidth == MaxColumnWidth, this column has fixed width and cannot be resized.
	bool bIsFixedColumnWidth() const { return MinColumnWidth == MaxColumnWidth; }

	FText GetFormattedValue(const FTimerNode& TimerNode) const
	{
		return GetFormattedValueFn(*this, TimerNode);
	}

protected:
	/// No default constructor.
	FTimersViewColumn() = delete;

	/// Initialization constructor, only used in FTimersViewColumnFactory.
	FTimersViewColumn
	(
		int32 InOrder,
		const FName InId,
		const FName InSearchId,
		FText InShortName,
		FText InTitleName,
		FText InDescription,
		bool bInIsVisible,
		const EFlags InFlags,
		const EHorizontalAlignment InHorizontalAlignment,
		const float InInitialColumnWidth,
		const float InMinColumnWidth,
		const float InMaxColumnWidth,
		const FGetFormattedValueFn InGetFormattedValueFn
	)
		: Order(InOrder)
		, Id(InId)
		, SearchId(InSearchId)
		, ShortName(MoveTemp(InShortName))
		, TitleName(MoveTemp(InTitleName))
		, Description(MoveTemp(InDescription))
		, bIsVisible(bInIsVisible)
		, Flags(InFlags)
		, HorizontalAlignment(InHorizontalAlignment)
		, InitialColumnWidth(InInitialColumnWidth)
		, MinColumnWidth(InMinColumnWidth)
		, MaxColumnWidth(InMaxColumnWidth)
		, GetFormattedValueFn(InGetFormattedValueFn)
	{
	}

public:
	/// Order value, to sort columns in the tree view.
	int32 Order;

	/// Name of the column, name of the property.
	FName Id;

	/// Name of the column used by the searching system.
	FName SearchId;

	/// Short name of the column, displayed in the column header.
	FText ShortName;

	/// Title name of the column, displayed as title in the column tooltip.
	FText TitleName;

	/// Long name of the column, displayed in the column tooltip.
	FText Description;

	/// Is this column visible?
	bool bIsVisible;

	/// On/off switches.
	EFlags Flags;

	/// Horizontal alignment of the content in this column.
	EHorizontalAlignment HorizontalAlignment;

	float InitialColumnWidth; /// Initial column width.
	float MinColumnWidth; /// Minimum column width.
	float MaxColumnWidth; /// Maximum column width.

	/// Custom function used to format (as an FText) the value of the timer node to be displayed by this column.
	FGetFormattedValueFn GetFormattedValueFn;
};
