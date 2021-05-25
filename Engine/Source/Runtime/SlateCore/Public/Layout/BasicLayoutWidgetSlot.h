// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlotBase.h"
#include "Layout/FlowDirection.h"
#include "Layout/Margin.h"
#include "Misc/Optional.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
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
	struct FSlotArgumentsMixin
	{
	private:
		friend TAlignmentWidgetSlotMixin;

	public:
		typename MixedIntoType::FSlotArguments& HAlign(EHorizontalAlignment InHAlignment)
		{
			_HAlignment = InHAlignment;
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}

		typename MixedIntoType::FSlotArguments& VAlign(EVerticalAlignment InVAlignment)
		{
			_VAlignment = InVAlignment;
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}

	private:
		TOptional<EHorizontalAlignment> _HAlignment;
		TOptional<EVerticalAlignment> _VAlignment;
	};

protected:
	void ConstructMixin(const FChildren& SlotOwner, FSlotArgumentsMixin&& InArgs)
	{
		HAlignment = InArgs._HAlignment.Get(HAlignment);
		VAlignment = InArgs._VAlignment.Get(VAlignment);
	}

public:
	// HAlign will be deprecated soon. Use SetVerticalAlignment, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& HAlign(EHorizontalAlignment InHAlignment)
	{
		HAlignment = InHAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}

	// VAlign will be deprecated soon. Use SetVerticalAlignment, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& VAlign(EVerticalAlignment InVAlignment)
	{
		VAlignment = InVAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}

public:
	void SetHorizontalAlignment(EHorizontalAlignment Alignment)
	{
		if (HAlignment != Alignment)
		{
			HAlignment = Alignment;
			static_cast<MixedIntoType*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		}
	}

	EHorizontalAlignment GetHorizontalAlignment() const
	{
		return HAlignment;
	}

	void SetVerticalAlignment(EVerticalAlignment Alignment)
	{
		if (VAlignment != Alignment)
		{
			VAlignment = Alignment;
			static_cast<MixedIntoType*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		}
	}

	EVerticalAlignment GetVerticalAlignment() const
	{
		return VAlignment;
	}

public:
	/** Horizontal positioning of child within the allocated slot */
	UE_DEPRECATED(5.0, "Direct access to HAlignment is now deprecated. Use the getter.")
	EHorizontalAlignment HAlignment;
	/** Vertical positioning of child within the allocated slot */
	UE_DEPRECATED(5.0, "Direct access to VAlignment is now deprecated. Use the getter.")
	EVerticalAlignment VAlignment;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


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
	struct FSlotArgumentsMixin
	{
	private:
		friend TPaddingWidgetSlotMixin;
		TAttribute<FMargin> _Padding;

	public:
		typename MixedIntoType::FSlotArguments& Padding(TAttribute<FMargin> InPadding)
		{
			_Padding = MoveTemp(InPadding);
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}

		typename MixedIntoType::FSlotArguments& Padding(float Uniform)
		{
			_Padding = FMargin(Uniform);
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}

		typename MixedIntoType::FSlotArguments& Padding(float Horizontal, float Vertical)
		{
			_Padding = FMargin(Horizontal, Vertical);
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}

		typename MixedIntoType::FSlotArguments& Padding(float Left, float Top, float Right, float Bottom)
		{
			_Padding = FMargin(Left, Top, Right, Bottom);
			return static_cast<typename MixedIntoType::FSlotArguments&>(*this);
		}
	};

protected:
	void ConstructMixin(const FChildren& SlotOwner, FSlotArgumentsMixin&& InArgs)
	{
		if (InArgs._Padding.IsSet())
		{
			SlotPadding = MoveTemp(InArgs._Padding);
		}
	}

public:
	// Padding will be deprecated soon. Use SetPadding, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& Padding(TAttribute<FMargin> InPadding)
	{
		SlotPadding = MoveTemp(InPadding);
		return *(static_cast<MixedIntoType*>(this));
	}

	// Padding will be deprecated soon. Use SetPadding, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& Padding(float Uniform)
	{
		SlotPadding = FMargin(Uniform);
		return *(static_cast<MixedIntoType*>(this));
	}

	// Padding will be deprecated soon. Use SetPadding, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& Padding(float Horizontal, float Vertical)
	{
		SlotPadding = FMargin(Horizontal, Vertical);
		return *(static_cast<MixedIntoType*>(this));
	}

	// Padding will be deprecated soon. Use SetPadding, or if you are using ChildSlot, construct a new slot with FSlotArguments
	MixedIntoType& Padding(float Left, float Top, float Right, float Bottom)
	{
		SlotPadding = FMargin(Left, Top, Right, Bottom);
		return *(static_cast<MixedIntoType*>(this));
	}

public:
	void SetPadding(TAttribute<FMargin> InPadding)
	{
		SlotPadding = MoveTemp(InPadding);
		static_cast<MixedIntoType*>(this)->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
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

	TBasicLayoutWidgetSlot(FChildren& InOwner)
		: TSlotBase<SlotType>(InOwner)
		, TPaddingWidgetSlotMixin<SlotType>()
		, TAlignmentWidgetSlotMixin<SlotType>()
	{
	}

	TBasicLayoutWidgetSlot(const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
		: TSlotBase<SlotType>()
		, TPaddingWidgetSlotMixin<SlotType>()
		, TAlignmentWidgetSlotMixin<SlotType>(InHAlign, InVAlign)
	{}

	TBasicLayoutWidgetSlot(FChildren& InOwner, const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
		: TSlotBase<SlotType>(InOwner)
		, TPaddingWidgetSlotMixin<SlotType>()
		, TAlignmentWidgetSlotMixin<SlotType>(InHAlign, InVAlign)
	{
	}

public:
	SLATE_SLOT_BEGIN_ARGS_TwoMixins(TBasicLayoutWidgetSlot, TSlotBase<SlotType>, TPaddingWidgetSlotMixin<SlotType>, TAlignmentWidgetSlotMixin<SlotType>)
	SLATE_SLOT_END_ARGS()

	void Construct(const FChildren& SlotOwner, FSlotArguments&& InArgs)
	{
		TPaddingWidgetSlotMixin<SlotType>::ConstructMixin(SlotOwner, MoveTemp(InArgs));
		TAlignmentWidgetSlotMixin<SlotType>::ConstructMixin(SlotOwner, MoveTemp(InArgs));
		TSlotBase<SlotType>::Construct(SlotOwner, MoveTemp(InArgs));
	}
};


/** The basic slot that can be used by layout. */
class FBasicLayoutWidgetSlot : public TBasicLayoutWidgetSlot<FBasicLayoutWidgetSlot>
{
public:
	using TBasicLayoutWidgetSlot<FBasicLayoutWidgetSlot>::TBasicLayoutWidgetSlot;
};
