// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FastUpdate/SlateInvalidationWidgetIndex.h"

class FSlateInvalidationWidgetList;

/**
 * SlateInvalidationWidgetIndex cannot be used to sort the widget since the ArrayIndex may not be in the expected order. (See the array as a double linked list).
 * SlateInvalidationWidgetSortOrder builds a unique number that represents the order of the widget.
 * The number is padded in a way to keep the order but not necessarily sequential.
 * It is valid until the next SlateInvalidationRoot::ProcessInvalidation()
 */
struct FSlateInvalidationWidgetSortOrder
{
private:
	uint32 Order = 0;

public:
	FSlateInvalidationWidgetSortOrder() = default;
	FSlateInvalidationWidgetSortOrder(const FSlateInvalidationWidgetList& List, FSlateInvalidationWidgetIndex Index);

	bool operator< (const FSlateInvalidationWidgetSortOrder Other) const { return Order < Other.Order; }
	bool operator<= (const FSlateInvalidationWidgetSortOrder Other) const { return Order <= Other.Order; }
	bool operator> (const FSlateInvalidationWidgetSortOrder Other) const { return Order > Other.Order; }
	bool operator>= (const FSlateInvalidationWidgetSortOrder Other) const { return Order >= Other.Order; }
	bool operator== (const FSlateInvalidationWidgetSortOrder Other) const { return Order == Other.Order; }
	bool operator!= (const FSlateInvalidationWidgetSortOrder Other) const { return Order != Other.Order; }
};