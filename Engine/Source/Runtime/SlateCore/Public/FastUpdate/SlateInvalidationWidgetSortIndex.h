// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FastUpdate/SlateInvalidationWidgetIndex.h"

class FSlateInvalidationWidgetList;

/**
 * SlateInvalidationWidgetIndex cannot be used to sort the widget since the ArrayIndex may not be in the expected order. (See the array as a double linked list).
 * FSlateInvalidationWidgetSortIndex builds a unique number that represents the order of the widget.
 * The number is padded in a way to keep the order but not necessarily sequential.
 * It is valid until the next SlateInvalidationRoot::ProcessInvalidation()
 */
struct FSlateInvalidationWidgetSortIndex
{
private:
	int32 Order = 0;

public:
	FSlateInvalidationWidgetSortIndex() = default;
	FSlateInvalidationWidgetSortIndex(const FSlateInvalidationWidgetList& List, FSlateInvalidationWidgetIndex Index);

	bool operator< (const FSlateInvalidationWidgetSortIndex Other) const { return Order < Other.Order; }
	bool operator<= (const FSlateInvalidationWidgetSortIndex Other) const { return Order <= Other.Order; }
	bool operator> (const FSlateInvalidationWidgetSortIndex Other) const { return Order > Other.Order; }
	bool operator>= (const FSlateInvalidationWidgetSortIndex Other) const { return Order >= Other.Order; }
	bool operator== (const FSlateInvalidationWidgetSortIndex Other) const { return Order == Other.Order; }
};