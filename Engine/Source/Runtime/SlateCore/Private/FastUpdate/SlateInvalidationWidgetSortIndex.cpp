// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/SlateInvalidationWidgetSortIndex.h"

#include "FastUpdate/SlateInvalidationWidgetList.h"

FSlateInvalidationWidgetSortIndex::FSlateInvalidationWidgetSortIndex(const FSlateInvalidationWidgetList& List, FSlateInvalidationWidgetIndex Index)
	: Order()
{
	static_assert(sizeof(Index.ArrayIndex) <= sizeof(int16), "The size of index need to bellow 16 to fit in the 32 bit order.");
	Order = (List.Data[Index.ArrayIndex].OrderIndex << (sizeof(Index.ArrayIndex) * 8)) + Index.ElementIndex;
}