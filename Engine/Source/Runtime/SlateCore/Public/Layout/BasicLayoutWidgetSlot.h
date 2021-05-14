// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlotBase.h"
#include "Layout/FlowDirection.h"
#include "Layout/Margin.h"
#include "Misc/Optional.h"

/** Mixin to add the alignment functionality to a base slot. */
template <typename MixedIntoType>
class TAlignmentWidgetSlotMixin
{
public:
	TAlignmentWidgetSlotMixin()
		: HAlignment(HAlign_Fill)
		, VAlignment(VAlign_Fill)
	{}

	TAlignmentWidgetSlotMixin(const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
		: HAlignment(InHAlign)
		, VAlignment(InVAlign)
	{}

public:
	MixedIntoType& HAlign(EHorizontalAlignment InHAlignment)
	{
		HAlignment = InHAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& VAlign(EVerticalAlignment InVAlignment)
	{
		VAlignment = InVAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}

public:
	/** Horizontal positioning of child within the allocated slot */
	EHorizontalAlignment HAlignment;
	/** Vertical positioning of child within the allocated slot */
	EVerticalAlignment VAlignment;
};


PRAGMA_DISABLE_DEPRECATION_WARNINGS
/** Mixin to add the padding functionality to a base slot. */
template <typename MixedIntoType>
class TPaddingWidgetSlotMixin
{
public:
	TPaddingWidgetSlotMixin() = default;
	TPaddingWidgetSlotMixin(const FMargin& Margin)
		: SlotPadding(Margin)
	{}

public:
	MixedIntoType& Padding(TAttribute<FMargin> InPadding)
	{
		SlotPadding = MoveTemp(InPadding);
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding(float Uniform)
	{
		SlotPadding = FMargin(Uniform);
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding(float Horizontal, float Vertical)
	{
		SlotPadding = FMargin(Horizontal, Vertical);
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding(float Left, float Top, float Right, float Bottom)
	{
		SlotPadding = FMargin(Left, Top, Right, Bottom);
		return *(static_cast<MixedIntoType*>(this));
	}

public:
	void SetPadding(TAttribute<FMargin> InPadding)
	{
		SlotPadding = MoveTemp(InPadding);
	}

	const FMargin& GetPadding() const { return SlotPadding.Get(); }

public:
	UE_DEPRECATED(5.0, "Direct access to SlotPadding is now deprecated. Use the setter or getter.")
	TAttribute<FMargin> SlotPadding;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


/** A templated basic slot that can be used by layout. */
template <typename SlotType>
class TBasicLayoutWidgetSlot : public TSlotBase<SlotType>
	, public TPaddingWidgetSlotMixin<SlotType>
	, public TAlignmentWidgetSlotMixin<SlotType>
{
public:
	TBasicLayoutWidgetSlot()
		: TSlotBase<SlotType>()
		, TPaddingWidgetSlotMixin<SlotType>()
		, TAlignmentWidgetSlotMixin<SlotType>()
	{}

	TBasicLayoutWidgetSlot(const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
		: TSlotBase<SlotType>()
		, TPaddingWidgetSlotMixin<SlotType>()
		, TAlignmentWidgetSlotMixin<SlotType>(InHAlign, InVAlign)
	{}
};


/** The basic slot that can be used by layout. */
class FBasicLayoutWidgetSlot : public TBasicLayoutWidgetSlot<FBasicLayoutWidgetSlot>
{
public:
	using TBasicLayoutWidgetSlot<FBasicLayoutWidgetSlot>::TBasicLayoutWidgetSlot;
};
