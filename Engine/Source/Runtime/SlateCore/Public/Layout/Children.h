// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SNullWidget.h"
#include "SlotBase.h"
#include "Layout/BasicLayoutWidgetSlot.h"
#include "Widgets/SWidget.h"

/**
 * FChildren is an interface that must be implemented by all child containers.
 * It allows iteration over a list of any Widget's children regardless of how
 * the underlying Widget happens to store its children.
 * 
 * FChildren is intended to be returned by the GetChildren() method.
 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
class SLATECORE_API FChildren
{
public:
	FChildren(SWidget* InOwner)
		: Owner(InOwner)
	{
		check(InOwner);
	}

	FChildren(std::nullptr_t) = delete;

	//~ Prevents allocation of FChilren. It created a confusion between FSlot and FChildren
	void* operator new (size_t) = delete;
	void* operator new[](size_t) = delete;

	/** @return the number of children */
	virtual int32 Num() const = 0;
	/** @return pointer to the Widget at the specified Index. */
	virtual TSharedRef<SWidget> GetChildAt( int32 Index ) = 0;
	/** @return const pointer to the Widget at the specified Index. */
	virtual TSharedRef<const SWidget> GetChildAt( int32 Index ) const = 0;

	SWidget& GetOwner() const { return *Owner; }

protected:
	friend class SWidget;
	friend class FCombinedChildren;
	/** @return the const reference to the slot at the specified Index */
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const = 0;

protected:
	virtual ~FChildren(){}

protected:
	UE_DEPRECATED(5.0, "Direct access to Owner is now deprecated. Use the getter.")
	SWidget* Owner;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


/**
 * Occasionally you may need to keep multiple discrete sets of children with differing slot requirements.
 * This data structure can be used to link multiple FChildren under a single accessor so you can always return
 * all children from GetChildren, but internally manage them in their own child lists.
 */
class FCombinedChildren : public FChildren
{
public:
	using FChildren::FChildren;

	void AddChildren(FChildren& InLinkedChildren)
	{
		LinkedChildren.Add(&InLinkedChildren);
	}

	/** @return the number of children */
	virtual int32 Num() const override
	{
		int32 TotalNum = 0;
		for (const FChildren* Children : LinkedChildren)
		{
			TotalNum += Children->Num();
		}

		return TotalNum;
	}

	virtual TSharedRef<SWidget> GetChildAt(int32 Index) override
	{
		return GetSlotAt(Index).GetWidget();
	}

	virtual TSharedRef<const SWidget> GetChildAt(int32 Index) const override
	{
		return GetSlotAt(Index).GetWidget();
	}

protected:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		int32 TotalNum = 0;
		for (const FChildren* Children : LinkedChildren)
		{
			const int32 NewTotal = TotalNum + Children->Num();
			if (NewTotal > ChildIndex)
			{
				return Children->GetSlotAt(ChildIndex - TotalNum);
			}
			TotalNum = NewTotal;
		}

		// This result should never occur users should always access a valid index for child slots.
		static FSlotBase NullSlot; check(false); return NullSlot;
	}

protected:
	TArray<FChildren*> LinkedChildren;
};


/**
 * Widgets with no Children can return an instance of FNoChildren.
 * For convenience a shared instance FNoChildren::NoChildrenInstance can be used.
 */
class SLATECORE_API FNoChildren : public FChildren
{
public:
	static FNoChildren NoChildrenInstance;

public:
	UE_DEPRECATED(5.0, "FNoChildren take a valid reference to a SWidget")
	FNoChildren();

	FNoChildren(SWidget* InOwner)
		: FChildren(InOwner)
	{
	}

	virtual int32 Num() const override { return 0; }
	
	virtual TSharedRef<SWidget> GetChildAt( int32 ) override
	{
		// Nobody should be getting a child when there aren't any children.
		// We expect this to crash!
		check( false );
		return SNullWidget::NullWidget;
	}
	
	virtual TSharedRef<const SWidget> GetChildAt( int32 ) const override
	{
		// Nobody should be getting a child when there aren't any children.
		// We expect this to crash!
		check( false );
		return SNullWidget::NullWidget;
	}

private:
	friend class SWidget;
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		check(false);
		static FSlotBase NullSlot;
		return NullSlot;
	}
};


/**
 * Widgets that will only have one child.
 */
template <typename MixedIntoType>
class UE_DEPRECATED(5.0, "TSupportsOneChildMixin is deprecated because it got confused between FSlot and FChildren. Use FSingleWidgetChildren.")
TSupportsOneChildMixin : public FChildren, public TSlotBase<MixedIntoType>
{
public:
	TSupportsOneChildMixin(SWidget* InOwner)
		: FChildren(InOwner)
		, TSlotBase<MixedIntoType>()
	{
		this->RawParentPtr = InOwner;
	}

	TSupportsOneChildMixin(std::nullptr_t) = delete;

	virtual int32 Num() const override { return 1; }

	virtual TSharedRef<SWidget> GetChildAt( int32 ChildIndex ) override
	{
		check(ChildIndex == 0);
		return FSlotBase::GetWidget();
	}

	virtual TSharedRef<const SWidget> GetChildAt( int32 ChildIndex ) const override
	{
		check(ChildIndex == 0);
		return FSlotBase::GetWidget();
	}

private:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override { check(ChildIndex == 0); return *this; }
};


/**
 * For widgets that do not own their content, but are responsible for presenting someone else's content.
 * e.g. Tooltips are just presented by the owner window; not actually owned by it. They can go away at any time
 *      and then they'll just stop being shown.
 */
template <typename ChildType>
class TWeakChild : public FChildren
{
public:
	using FChildren::FChildren;

	virtual int32 Num() const override { return WidgetPtr.IsValid() ? 1 : 0 ; }

	virtual TSharedRef<SWidget> GetChildAt( int32 ChildIndex ) override
	{
		check(ChildIndex == 0);
		return GetWidget();
	}

	virtual TSharedRef<const SWidget> GetChildAt( int32 ChildIndex ) const override
	{
		check(ChildIndex == 0);
		return GetWidget();
	}

private:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override { static FSlotBase NullSlot; check(ChildIndex == 0); return NullSlot; }

public:
	void AttachWidget(const TSharedPtr<SWidget>& InWidget)
	{
		WidgetPtr = InWidget;
		GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);

		if (InWidget.IsValid() && InWidget != SNullWidget::NullWidget)
		{
			InWidget->AssignParentWidget(GetOwner().AsShared());
		}
	}

	void DetachWidget()
	{
		if (WidgetPtr.IsValid())
		{
			TSharedPtr<SWidget> Widget = WidgetPtr.Pin();

			if (Widget != SNullWidget::NullWidget)
			{
				Widget->ConditionallyDetatchParentWidget(&GetOwner());
			}

			WidgetPtr.Reset();
		}
	}

	TSharedRef<SWidget> GetWidget() const
	{
		ensure(Num() > 0);
		TSharedPtr<SWidget> Widget = WidgetPtr.Pin();
		return (Widget.IsValid()) ? Widget.ToSharedRef() : SNullWidget::NullWidget;
	}

private:
	SWidget& GetWidgetRef() const
	{
		ensure(Num() > 0);
		SWidget* Widget = WidgetPtr.Pin().Get();
		return Widget ? *Widget : SNullWidget::NullWidget.Get();
	}

private:
	TWeakPtr<ChildType> WidgetPtr;
};


template <typename MixedIntoType>
class UE_DEPRECATED(5.0, "Renamed TSupportsContentPaddingMixin to TAlignmentWidgetSlotMixin to differenciate from FSlot and FChildren.")
TSupportsContentAlignmentMixin : public TAlignmentWidgetSlotMixin<MixedIntoType>
{
	using TAlignmentWidgetSlotMixin<MixedIntoType>::TAlignmentWidgetSlotMixin;
};

template <typename MixedIntoType>
class UE_DEPRECATED(5.0, "Renamed TSupportsContentPaddingMixin to TPaddingWidgetSlotMixin to differenciate from FSlot and FChildren.")
TSupportsContentPaddingMixin : public TPaddingWidgetSlotMixin<MixedIntoType>
{
	using TPaddingWidgetSlotMixin<MixedIntoType>::TPaddingWidgetSlotMixin;
};


/** A FChildren that has only one child and can take a templated slot. */
template<typename SlotType>
class TSingleWidgetChildrenWithSlot : public FChildren, protected TSlotBase<SlotType>
{
public:
	TSingleWidgetChildrenWithSlot(SWidget* InOwner)
		: FChildren(InOwner)
		, TSlotBase<SlotType>()
	{
		this->RawParentPtr = InOwner;
	}

	TSingleWidgetChildrenWithSlot(std::nullptr_t) = delete;

public:
	virtual int32 Num() const override { return 1; }

	virtual TSharedRef<SWidget> GetChildAt(int32 ChildIndex) override
	{
		check(ChildIndex == 0);
		return this->GetWidget();
	}

	virtual TSharedRef<const SWidget> GetChildAt(int32 ChildIndex) const override
	{
		check(ChildIndex == 0);
		return this->GetWidget();
	}

public:
	TSlotBase<SlotType>& AsSlot() { return *this; }
	const TSlotBase<SlotType>& AsSlot() const { return *this; }

	using TSlotBase<SlotType>::AttachWidget;
	using TSlotBase<SlotType>::DetachWidget;
	using TSlotBase<SlotType>::GetWidget;
	SlotType& operator[](const TSharedRef<SWidget>& InChildWidget)
	{
		this->AttachWidget(InChildWidget);
		return static_cast<SlotType&>(*this);
	}

	SlotType& Expose(SlotType*& OutVarToInit)
	{
		OutVarToInit = static_cast<SlotType*>(this);
		return static_cast<SlotType&>(*this);
	}

private:
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		check(ChildIndex == 0);
		return *this;
	}
};


/** A FChildren that has only one child. */
class FSingleWidgetChildrenWithSlot : public TSingleWidgetChildrenWithSlot<FSingleWidgetChildrenWithSlot>
{
public:
	using TSingleWidgetChildrenWithSlot<FSingleWidgetChildrenWithSlot>::TSingleWidgetChildrenWithSlot;
};


/** A FChildren that has only one child and support alignment and padding. */
class FSingleWidgetChildrenWithBasicLayoutSlot : public TSingleWidgetChildrenWithSlot<FSingleWidgetChildrenWithBasicLayoutSlot>
	, public TPaddingWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>
	, public TAlignmentWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>
{
public:
	FSingleWidgetChildrenWithBasicLayoutSlot(SWidget* InOwner)
		: TSingleWidgetChildrenWithSlot<FSingleWidgetChildrenWithBasicLayoutSlot>(InOwner)
		, TPaddingWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>()
		, TAlignmentWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>(HAlign_Fill, VAlign_Fill)
	{
	}

	FSingleWidgetChildrenWithBasicLayoutSlot(SWidget* InOwner, const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
		: TSingleWidgetChildrenWithSlot<FSingleWidgetChildrenWithBasicLayoutSlot>(InOwner)
		, TPaddingWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>()
		, TAlignmentWidgetSlotMixin<FSingleWidgetChildrenWithBasicLayoutSlot>(InHAlign, InVAlign)
	{
	}

	FSingleWidgetChildrenWithBasicLayoutSlot(std::nullptr_t) = delete;
	FSingleWidgetChildrenWithBasicLayoutSlot(std::nullptr_t, const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign) = delete;
};


/** A slot that support alignment of content and padding */
class UE_DEPRECATED(5.0, "FSimpleSlot is deprecated because it got confused from FChildren with FSlot. Use FSingleWidgetChildrenWithSimpleSlot.")
FSimpleSlot : public FSingleWidgetChildrenWithBasicLayoutSlot
{
public:
	using FSingleWidgetChildrenWithBasicLayoutSlot::FSingleWidgetChildrenWithBasicLayoutSlot;
};


/**
 * A generic FChildren that stores children along with layout-related information.
 * The type containing Widget* and layout info is specified by ChildType.
 * ChildType must have a public member SWidget* Widget;
 */
template<typename SlotType>
class TPanelChildren : public FChildren
{
private:
	TArray<TUniquePtr<SlotType>> Children;

	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		return *Children[ChildIndex];
	}

public:
	using FChildren::FChildren;
	
	virtual int32 Num() const override
	{
		return Children.Num();
	}

	virtual TSharedRef<SWidget> GetChildAt( int32 Index ) override
	{
		return Children[Index]->GetWidget();
	}

	virtual TSharedRef<const SWidget> GetChildAt( int32 Index ) const override
	{
		return Children[Index]->GetWidget();
	}

	int32 Add( SlotType* Slot )
	{
		int32 Index = Children.Add(TUniquePtr<SlotType>(Slot));
		check(Slot);
		Slot->AttachWidgetParent(&GetOwner());

		return Index;
	}

	void RemoveAt( int32 Index )
	{
		// NOTE:
		// We don't do any invalidating here, that's handled by the FSlotBase, which eventually calls ConditionallyDetatchParentWidget

		// Steal the instance from the array, then free the element.
		// This alleviates issues where (misbehaving) destructors on the children may call back into this class and query children while they are being destroyed.
		TUniquePtr<SlotType> SlotToRemove = MoveTemp(Children[Index]);
		Children.RemoveAt(Index);
		SlotToRemove.Reset();
	}

	/** Removes the corresponding widget from the set of children if it exists.  Returns the index it found the child at, INDEX_NONE otherwise. */
	int32 Remove(const TSharedRef<SWidget>& SlotWidget)
	{
		for (int32 SlotIdx = 0; SlotIdx < Num(); ++SlotIdx)
		{
			if (SlotWidget == Children[SlotIdx]->GetWidget())
			{
				Children.RemoveAt(SlotIdx);
				return SlotIdx;
			}
		}

		return INDEX_NONE;
	}

	void Empty(int32 Slack = 0)
	{
		// NOTE:
		// We don't do any invalidating here, that's handled by the FSlotBase, which eventually calls ConditionallyDetatchParentWidget

		// We empty children by first transferring them onto a stack-owned array, then freeing the elements.
		// This alleviates issues where (misbehaving) destructors on the children may call back into this class and query children while they are being destroyed.
		// By storing the children on the stack first, we defer the destruction of children until after we have emptied our owned container.
		TArray<TUniquePtr<SlotType>> ChildrenCopy = MoveTemp(Children);

		// Explicitly calling Empty is not really necessary (it is already empty/moved-from now), but we call it for safety
		Children.Empty();

		// ChildrenCopy will now be emptied and moved back (to preserve any allocated memory)
		ChildrenCopy.Empty(Slack);
		Children = MoveTemp(ChildrenCopy);
	}

	void Insert(SlotType* Slot, int32 Index)
	{
		check(Slot);
		Children.Insert(TUniquePtr<SlotType>(Slot), Index);
		Slot->AttachWidgetParent(&GetOwner());
	}

	void Move(int32 IndexToMove, int32 IndexToDestination)
	{
		// @todo this is going to cause a problem for draw ordering

		{
			TUniquePtr<SlotType> SlotToMove = MoveTemp(Children[IndexToMove]);
			Children.RemoveAt(IndexToMove);
			Children.Insert(MoveTemp(SlotToMove), IndexToDestination);
		}

		GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

	void Reserve( int32 NumToReserve )
	{
		Children.Reserve(NumToReserve);
	}

	bool IsValidIndex( int32 Index ) const
	{
		return Children.IsValidIndex( Index );
	}

	const SlotType& operator[](int32 Index) const { return *Children[Index]; }
	SlotType& operator[](int32 Index) { return *Children[Index]; }

	template <class PREDICATE_CLASS>
	void Sort( const PREDICATE_CLASS& Predicate )
	{
		Children.Sort([&Predicate](const TUniquePtr<SlotType>& One, const TUniquePtr<SlotType>& Two)
		{
			return Predicate(*One, *Two);
		});
		GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

	template <class PREDICATE_CLASS>
	void StableSort(const PREDICATE_CLASS& Predicate)
	{
		Children.StableSort([&Predicate](const TUniquePtr<SlotType>& One, const TUniquePtr<SlotType>& Two)
		{
			return Predicate(*One, *Two);
		});
		GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
	}

	void Swap( int32 IndexA, int32 IndexB )
	{
		Children.Swap(IndexA, IndexB);
		GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
	}
};



template<typename SlotType>
class TPanelChildrenConstIterator
{
public:
	TPanelChildrenConstIterator(const TPanelChildren<SlotType>& InContainer, EFlowDirection InLayoutFlow)
		: Container(InContainer)
		, LayoutFlow(InLayoutFlow)
	{
		Reset();
	}

	TPanelChildrenConstIterator(const TPanelChildren<SlotType>& InContainer, EOrientation InOrientation, EFlowDirection InLayoutFlow)
		: Container(InContainer)
		, LayoutFlow(InOrientation == Orient_Vertical ? EFlowDirection::LeftToRight : InLayoutFlow)
	{
		Reset();
	}

	/** Advances iterator to the next element in the container. */
	TPanelChildrenConstIterator<SlotType>& operator++()
	{
		switch (LayoutFlow)
		{
		default:
		case EFlowDirection::LeftToRight:
			++Index;
			break;
		case EFlowDirection::RightToLeft:
			--Index;
			break;
		}

		return *this;
	}

	/** Moves iterator to the previous element in the container. */
	TPanelChildrenConstIterator<SlotType>& operator--()
	{
		switch (LayoutFlow)
		{
		default:
		case EFlowDirection::LeftToRight:
			--Index;
			break;
		case EFlowDirection::RightToLeft:
			++Index;
			break;
		}

		return *this;
	}

	const SlotType& operator* () const
	{
		return Container[Index];
	}

	const SlotType* operator->() const
	{
		return &Container[Index];
	}

	/** conversion to "bool" returning true if the iterator has not reached the last element. */
	FORCEINLINE explicit operator bool() const
	{
		return Container.IsValidIndex(Index);
	}

	/** Returns an index to the current element. */
	int32 GetIndex() const
	{
		return Index;
	}

	/** Resets the iterator to the first element. */
	void Reset()
	{
		switch (LayoutFlow)
		{
		default:
		case EFlowDirection::LeftToRight:
			Index = 0;
			break;
		case EFlowDirection::RightToLeft:
			Index = Container.Num() - 1;
			break;
		}
	}

	/** Sets iterator to the last element. */
	void SetToEnd()
	{
		switch (LayoutFlow)
		{
		default:
		case EFlowDirection::LeftToRight:
			Index = Container.Num() - 1;
			break;
		case EFlowDirection::RightToLeft:
			Index = 0;
			break;
		}
	}

private:

	const TPanelChildren<SlotType>& Container;
	int32 Index;
	EFlowDirection LayoutFlow;
};



/**
 * Some advanced widgets contain no layout information, and do not require slots.
 * Those widgets may wish to store a specialized type of child widget.
 * In those cases, using TSlotlessChildren is convenient.
 *
 * TSlotlessChildren should not be used for general-purpose widgets.
 */
template<typename ChildType>
class TSlotlessChildren : public FChildren
{
private:
	TArray<TSharedRef<ChildType>> Children;

	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override
	{
		// @todo slate : slotless children should be removed altogether; for now they return a fake slot.
		static FSlotBase NullSlot;
		return NullSlot;
	}

public:
	TSlotlessChildren(SWidget* InOwner, bool InbChangesInvalidatePrepass = true)
		: FChildren(InOwner)
		, bChangesInvalidatePrepass(InbChangesInvalidatePrepass)
	{
	}

	TSlotlessChildren(std::nullptr_t, bool InbChangesInvalidatePrepass = true) = delete;

	virtual int32 Num() const override
	{
		return Children.Num();
	}

	virtual TSharedRef<SWidget> GetChildAt( int32 Index ) override
	{
		return Children[Index];
	}

	virtual TSharedRef<const SWidget> GetChildAt( int32 Index ) const override
	{
		return Children[Index];
	}

	int32 Add( const TSharedRef<ChildType>& Child )
	{
		if (bChangesInvalidatePrepass)
		{ 
			GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
		}

		int32 Index = Children.Add(Child);

		if (Child != SNullWidget::NullWidget)
		{
			Child->AssignParentWidget(GetOwner().AsShared());
		}

		return Index;
	}

	void Reset(int32 NewSize = 0)
	{
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ChildIndex++)
		{
			TSharedRef<SWidget> Child = GetChildAt(ChildIndex);
			if (Child != SNullWidget::NullWidget)
			{
				Child->ConditionallyDetatchParentWidget(&GetOwner());
			}
		}

		// We reset children by first transferring them onto a stack-owned array, then freeing the elements.
		// This alleviates issues where (misbehaving) destructors on the children may call back into this class and query children while they are being destroyed.
		// By storing the children on the stack first, we defer the destruction of children until after we have reset our owned container.
		TArray<TSharedRef<ChildType>> ChildrenCopy = MoveTemp(Children);

		// Explicitly calling Reset is not really necessary (it is already empty/moved-from now), but we call it for safety
		Children.Reset();

		// ChildrenCopy will now be reset and moved back (to preserve any allocated memory)
		ChildrenCopy.Reset(NewSize);
		Children = MoveTemp(ChildrenCopy);
	}

	void Empty(int32 Slack = 0)
	{
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ChildIndex++)
		{
			TSharedRef<SWidget> Child = GetChildAt(ChildIndex);
			if (Child != SNullWidget::NullWidget)
			{
				Child->ConditionallyDetatchParentWidget(&GetOwner());
			}
		}

		// We empty children by first transferring them onto a stack-owned array, then freeing the elements.
		// This alleviates issues where (misbehaving) destructors on the children may call back into this class and query children while they are being destroyed.
		// By storing the children on the stack first, we defer the destruction of children until after we have emptied our owned container.
		TArray<TSharedRef<ChildType>> ChildrenCopy = MoveTemp(Children);

		// Explicitly calling Empty is not really necessary (it is already empty/moved-from now), but we call it for safety
		Children.Empty();

		// ChildrenCopy will now be emptied and moved back (to preserve any allocated memory)
		ChildrenCopy.Empty(Slack);
		Children = MoveTemp(ChildrenCopy);
	}

	void Insert(const TSharedRef<ChildType>& Child, int32 Index)
	{
		if (bChangesInvalidatePrepass) 
		{
			GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
		}

		Children.Insert(Child, Index);
		if (Child != SNullWidget::NullWidget)
		{
			Child->AssignParentWidget(GetOwner().AsShared());
		}
	}

	int32 Remove( const TSharedRef<ChildType>& Child )
	{
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(&GetOwner());
		}

		return Children.Remove( Child );
	}

	void RemoveAt( int32 Index )
	{
		TSharedRef<SWidget> Child = GetChildAt(Index);
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(&GetOwner());
		}

		// Note: Child above ensures the instance we're removing from the array won't run its destructor until after it's fully removed from the array
		Children.RemoveAt( Index );
	}

	int32 Find( const TSharedRef<ChildType>& Item ) const
	{
		return Children.Find( Item );
	}

	TArray< TSharedRef< ChildType > > AsArrayCopy() const
	{
		return TArray<TSharedRef<ChildType>>(Children);
	}

	const TSharedRef<ChildType>& operator[](int32 Index) const { return Children[Index]; }
	TSharedRef<ChildType>& operator[](int32 Index) { return Children[Index]; }

	template <class PREDICATE_CLASS>
	void Sort( const PREDICATE_CLASS& Predicate )
	{
		Children.Sort( Predicate );
		if (bChangesInvalidatePrepass)
		{
			GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
		}
	}

	void Swap( int32 IndexA, int32 IndexB )
	{
		Children.Swap(IndexA, IndexB);
		if (bChangesInvalidatePrepass)
		{
			GetOwner().Invalidate(EInvalidateWidgetReason::ChildOrder);
		}
	}

private:
	bool bChangesInvalidatePrepass;
};


/** A FChildren that support only one slot and alignment of content and padding */
class FOneSimpleMemberChild : public TSingleWidgetChildrenWithSlot<FOneSimpleMemberChild>
	, public TAlignmentWidgetSlotMixin<FOneSimpleMemberChild>
{
public:
	template<typename WidgetType, typename V = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
	FOneSimpleMemberChild(WidgetType& InParent)
		: TSingleWidgetChildrenWithSlot<FOneSimpleMemberChild>(&InParent)
		, TAlignmentWidgetSlotMixin<FOneSimpleMemberChild>(HAlign_Fill, VAlign_Fill)
		, SlotPaddingAttribute(InParent)
	{
	}

	//~ TSlateAttribute cannot be copied
	FOneSimpleMemberChild(const FOneSimpleMemberChild&) = delete;
	FOneSimpleMemberChild& operator=(const FOneSimpleMemberChild&) = delete;

	FOneSimpleMemberChild& Padding(TAttribute<FMargin> InPadding)
	{
		SlotPaddingAttribute.Assign(FChildren::GetOwner(), MoveTemp(InPadding));
		return *this;
	}

	FOneSimpleMemberChild& Padding(float Uniform)
	{
		SlotPaddingAttribute.Set(FChildren::GetOwner(), FMargin(Uniform));
		return *this;
	}

	FOneSimpleMemberChild& Padding(float Horizontal, float Vertical)
	{
		SlotPaddingAttribute.Set(FChildren::GetOwner(), FMargin(Horizontal, Vertical));
		return *this;
	}

	FOneSimpleMemberChild& Padding(float Left, float Top, float Right, float Bottom)
	{
		SlotPaddingAttribute.Set(FChildren::GetOwner(), FMargin(Left, Top, Right, Bottom));
		return *this;
	}

	void SetPadding(TAttribute<FMargin> InPadding)
	{
		SlotPaddingAttribute.Assign(FChildren::GetOwner(), MoveTemp(InPadding));
	}
	const FMargin& GetPadding() const { return SlotPaddingAttribute.Get(); }

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.0, "Direct access to SlotPadding is now deprecated. Use the setter or getter.")
	FSlateDeprecatedTAttribute<FMargin> SlotPadding;
#endif

public:
	using SlotPaddingAttributeType = SlateAttributePrivate::TSlateMemberAttribute<FMargin, ::SlateAttributePrivate::FSlateAttributeNoInvalidationReason, TSlateAttributeComparePredicate<>>;
	using SlotPaddingAttributeRefType = SlateAttributePrivate::TSlateMemberAttributeRef<SlotPaddingAttributeType>;

	template<typename WidgetType, typename V = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
	SlotPaddingAttributeRefType GetSlotPaddingAttribute() const
	{
		WidgetType&  Widget = static_cast<WidgetType&>(FChildren::GetOwner());
		return SlotPaddingAttributeRefType(Widget.template SharedThis<WidgetType>(&Widget), SlotPaddingAttribute);
	}

protected:
	SlotPaddingAttributeType SlotPaddingAttribute;
};


/** Required to implement GetChildren() in a way that can dynamically return the currently active child. */
template<typename SlotType>
class TOneDynamicChild : public FChildren
{
public:
	TOneDynamicChild(SWidget* InOwner, TPanelChildren<SlotType>* InAllChildren, const TAttribute<int32>* InWidgetIndex)
		: FChildren(InOwner)
		, AllChildren(InAllChildren)
		, WidgetIndex(InWidgetIndex)
	{
		check(InAllChildren);
		check(WidgetIndex);
	}

	TOneDynamicChild(SWidget* InOwner, TPanelChildren<SlotType>* InAllChildren, std::nullptr_t) = delete;
	TOneDynamicChild(SWidget* InOwner, std::nullptr_t, const TAttribute<int32>* InWidgetIndex) = delete;
	TOneDynamicChild(SWidget* InOwner, std::nullptr_t, std::nullptr_t) = delete;
	TOneDynamicChild(std::nullptr_t, TPanelChildren<SlotType>* InAllChildren, std::nullptr_t) = delete;
	TOneDynamicChild(std::nullptr_t, std::nullptr_t, const TAttribute<int32>* InWidgetIndex) = delete;
	TOneDynamicChild(std::nullptr_t, std::nullptr_t, std::nullptr_t) = delete;

	virtual int32 Num() const override { return AllChildren->Num() > 0 ? 1 : 0; }

	virtual TSharedRef<SWidget> GetChildAt(int32 Index) override
	{
		check(Index == 0); return AllChildren->GetChildAt(WidgetIndex->Get());
	}

	virtual TSharedRef<const SWidget> GetChildAt(int32 Index) const override
	{
		check(Index == 0);
		return AllChildren->GetChildAt(WidgetIndex->Get());
	}

private:

	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const override { return (*AllChildren)[ChildIndex]; }

	TPanelChildren<SlotType>* AllChildren;
	const TAttribute<int32>* WidgetIndex;
};