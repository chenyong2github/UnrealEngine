// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/InvalidateWidgetReason.h"

class SWidget;
class FChildren;

/** Slot are a container of a SWidget used by the FChildren. */
class SLATECORE_API FSlotBase
{
public:
	FSlotBase();
	FSlotBase(const FChildren& Children);
	FSlotBase(const TSharedRef<SWidget>& InWidget);
	FSlotBase& operator=(const FSlotBase&) = delete;
	FSlotBase(const FSlotBase&) = delete;

	virtual ~FSlotBase();

	UE_DEPRECATED(5.0, "AttachWidgetParent is not used anymore. Use get SetOwner.")
	void AttachWidgetParent(SWidget* InParent) { }

	/**
	 * Access the FChildren that own the slot.
	 * The owner can be invalid when the slot is not attached.
	 */
	const FChildren* GetOwner() const { return Owner; }

	/**
	 * Access the widget that own the slot.
	 * The owner can be invalid when the slot is not attached.
	 */
	SWidget* GetOwnerWidget() const;

	/** Set the owner of the slot. */
	void SetOwner(const FChildren& Children);

	FORCEINLINE_DEBUGGABLE void AttachWidget( const TSharedRef<SWidget>& InWidget )
	{
		// TODO: If we don't hold a reference here, ~SWidget() could called on the old widget before the assignment takes place
		// The behavior of TShareRef is going to change in the near future to avoid this issue and this should then be reverted.
		TSharedRef<SWidget> LocalWidget = Widget;
		DetatchParentFromContent();
		Widget = InWidget;
		AfterContentOrOwnerAssigned();
	}

	/**
	 * Access the widget in the current slot.
	 * There will always be a widget in the slot; sometimes it is
	 * the SNullWidget instance.
	 */
	FORCEINLINE_DEBUGGABLE const TSharedRef<SWidget>& GetWidget() const { return Widget; }

	/**
	 * Remove the widget from its current slot.
	 * The removed widget is returned so that operations could be performed on it.
	 * If the null widget was being stored, an invalid shared ptr is returned instead.
	 */
	const TSharedPtr<SWidget> DetachWidget();

	void Invalidate(EInvalidateWidgetReason InvalidateReason);

protected:
	/**
	 * Performs the attribute assignment and invalidates the widget minimally based on what actually changed.  So if the boundness of the attribute didn't change
	 * volatility won't need to be recalculated.  Returns true if the value changed.
	 */
	template<typename TargetValueType, typename SourceValueType>
	bool SetAttribute(TAttribute<TargetValueType>& TargetValue, const TAttribute<SourceValueType>& SourceValue, EInvalidateWidgetReason BaseInvalidationReason)
	{
		if (!TargetValue.IdenticalTo(SourceValue))
		{
			const bool bWasBound = TargetValue.IsBound();
			const bool bBoundnessChanged = bWasBound != SourceValue.IsBound();
			TargetValue = SourceValue;

			EInvalidateWidgetReason InvalidateReason = BaseInvalidationReason;
			if (bBoundnessChanged)
			{
				InvalidateReason |= EInvalidateWidgetReason::Volatility;
			}

			Invalidate(InvalidateReason);
			return true;
		}

		return false;
	}

private:
	void DetatchParentFromContent();
	void AfterContentOrOwnerAssigned();

private:
	/** The children that own the slot. */
	const FChildren* Owner;
	/** The content widget of the slot. */
	TSharedRef<SWidget> Widget;

#if WITH_EDITORONLY_DATA
protected:
	/** The parent and owner of the slot. */
	UE_DEPRECATED(5.0, "RawParentPtr is not used anymore. Use get Owner Widget.")
	SWidget* RawParentPtr;
#endif
};


/** A slot that can be used by the declarative syntax. */
template<typename SlotType>
class TSlotBase : public FSlotBase
{
public:
	using FSlotBase::FSlotBase;

	SlotType& operator[]( const TSharedRef<SWidget>& InChildWidget )
	{
		this->AttachWidget(InChildWidget);
		return static_cast<SlotType&>(*this);
	}

	SlotType& Expose( SlotType*& OutVarToInit )
	{
		OutVarToInit = static_cast<SlotType*>(this);
		return static_cast<SlotType&>(*this);
	}
};
