// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimerGroupingAndSorting.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains helper functions and classes for ETimerNodeType enum. */
struct TimerNodeTypeHelper
{
	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation of the specified ETimerNodeType value.
	 */
	static FText ToText(const ETimerNodeType Type);

	/**
	 * @param Type - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified ETimerNodeType value.
	 */
	static FText ToDescription(const ETimerNodeType Type);

	/**
	 * @param Type - The value to get the brush name for.
	 *
	 * @return brush name of the specified ETimerNodeType value.
	 */
	static FName ToBrushName(const ETimerNodeType Type);

	static const FSlateBrush* GetIconForGroup();
	static const FSlateBrush* GetIconForTimerNodeType(const ETimerNodeType Type);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Helper struct that contains grouping static functions and classes. */
struct TimerNodeGroupingHelper
{
	/**
	 * @param TimerGroupingMode - The value to get the text for.
	 *
	 * @return text representation of the specified ETimerGroupingMode value.
	 */
	static FText ToText(const ETimerGroupingMode TimerGroupingMode);

	/**
	 * @param TimerGroupingMode - The value to get the text for.
	 *
	 * @return text representation with more detailed explanation of the specified ETimerGroupingMode value.
	 */
	static FText ToDescription(const ETimerGroupingMode TimerGroupingMode);

	/**
	 * @param TimerGroupingMode - The value to get the brush name for.
	 *
	 * @return brush name of the specified ETimerGroupingMode value.
	 */
	static FName ToBrushName(const ETimerGroupingMode TimerGroupingMode);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
