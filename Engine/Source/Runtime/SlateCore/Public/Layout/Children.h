// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Margin.h"
#include "Widgets/SNullWidget.h"
#include "SlotBase.h"
#include "Widgets/SWidget.h"
#include "FlowDirection.h"

/**
 * FChildren is an interface that must be implemented by all child containers.
 * It allows iteration over a list of any Widget's children regardless of how
 * the underlying Widget happens to store its children.
 * 
 * FChildren is intended to be returned by the GetChildren() method.
 * 
 */
class SLATECORE_API FChildren
{
public:
	FChildren(SWidget* InOwner)
		: Owner(InOwner)
	{
	}

	/** @return the number of children */
	virtual int32 Num() const = 0;
	/** @return pointer to the Widget at the specified Index. */
	virtual TSharedRef<SWidget> GetChildAt( int32 Index ) = 0;
	/** @return const pointer to the Widget at the specified Index. */
	virtual TSharedRef<const SWidget> GetChildAt( int32 Index ) const = 0;

protected:
	friend class SWidget;
	friend class FCombinedChildren;
	/** @return the const reference to the slot at the specified Index */
	virtual const FSlotBase& GetSlotAt(int32 ChildIndex) const = 0;

protected:
	virtual ~FChildren(){}

protected:
	SWidget* Owner;
};


/**
 * Occasionally you may need to keep multiple descrete sets of children with differing slot requirements.
 * This datastructure can be used to link multiple FChildren under a single accessor so you can always return
 * all children from GetChildren, but internally manage them in their own child lists.
 */
class FCombinedChildren : public FChildren
{
public:
	FCombinedChildren(SWidget* InOwner)
		: FChildren(InOwner)
	{
	}

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
 * For convenience a shared instance SWidget::NoChildrenInstance can be used.
 */
class SLATECORE_API FNoChildren : public FChildren
{
public:
	FNoChildren()
		: FChildren(nullptr)
	{}

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
 * Widgets that will only have one child can return an instance of FOneChild.
 */
template <typename MixedIntoType>
class TSupportsOneChildMixin : public FChildren, public TSlotBase<MixedIntoType>
{
public:
	TSupportsOneChildMixin(SWidget* InOwner)
		: FChildren(InOwner)
		, TSlotBase<MixedIntoType>()
	{
		this->RawParentPtr = InOwner;
	}

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
	TWeakChild(SWidget* InOwner)
		: FChildren(InOwner)
		, WidgetPtr()
	{
	}

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
		if (Owner) 
		{ 
			Owner->Invalidate(EInvalidateWidget::ChildOrder);

			if (InWidget.IsValid() && InWidget != SNullWidget::NullWidget)
			{
				InWidget->AssignParentWidget(Owner->AsShared());
			}
		}
	}

	void DetachWidget()
	{
		if (WidgetPtr.IsValid())
		{
			TSharedPtr<SWidget> Widget = WidgetPtr.Pin();

			if (Widget != SNullWidget::NullWidget)
			{
				Widget->ConditionallyDetatchParentWidget(Owner);
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
class TSupportsContentAlignmentMixin
{
public:
	TSupportsContentAlignmentMixin(const EHorizontalAlignment InHAlign, const EVerticalAlignment InVAlign)
	: HAlignment( InHAlign )
	, VAlignment( InVAlign )
	{
		
	}

	MixedIntoType& HAlign( EHorizontalAlignment InHAlignment )
	{
		HAlignment = InHAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& VAlign( EVerticalAlignment InVAlignment )
	{
		VAlignment = InVAlignment;
		return *(static_cast<MixedIntoType*>(this));
	}
	
	EHorizontalAlignment HAlignment;
	EVerticalAlignment VAlignment;
};

template <typename MixedIntoType>
class TSupportsContentPaddingMixin
{
public:
	MixedIntoType& Padding( const TAttribute<FMargin> InPadding )
	{
		SlotPadding = InPadding;
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding( float Uniform )
	{
		SlotPadding = FMargin(Uniform);
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding( float Horizontal, float Vertical )
	{
		SlotPadding = FMargin(Horizontal, Vertical);
		return *(static_cast<MixedIntoType*>(this));
	}

	MixedIntoType& Padding( float Left, float Top, float Right, float Bottom )
	{
		SlotPadding = FMargin(Left, Top, Right, Bottom);
		return *(static_cast<MixedIntoType*>(this));
	}

	TAttribute< FMargin > SlotPadding;
};

/** A slot that support alignment of content and padding */
class SLATECORE_API FSimpleSlot : public TSupportsOneChildMixin<FSimpleSlot>, public TSupportsContentAlignmentMixin<FSimpleSlot>, public TSupportsContentPaddingMixin<FSimpleSlot>
{
public:
	FSimpleSlot(SWidget* InParent)
	: TSupportsOneChildMixin<FSimpleSlot>(InParent)
	, TSupportsContentAlignmentMixin<FSimpleSlot>(HAlign_Fill, VAlign_Fill)
	{
	}
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
	TPanelChildren(SWidget* InOwner)
		: FChildren(InOwner)
	{
	}
	
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

		if (Owner)
		{
			Slot->AttachWidgetParent(Owner);
		}

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
		Children.Insert(TUniquePtr<SlotType>(Slot), Index);

		// Don't do parent manipulation if this panel has no owner.
		if (Owner)
		{
			Slot->AttachWidgetParent(Owner);
		}
	}

	void Move(int32 IndexToMove, int32 IndexToDestination)
	{
		// @todo this is going to cause a problem for draw ordering

		{
			TUniquePtr<SlotType> SlotToMove = MoveTemp(Children[IndexToMove]);
			Children.RemoveAt(IndexToMove);
			Children.Insert(MoveTemp(SlotToMove), IndexToDestination);
		}

		if (Owner)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
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
		if (Owner)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	template <class PREDICATE_CLASS>
	void StableSort(const PREDICATE_CLASS& Predicate)
	{
		Children.StableSort([&Predicate](const TUniquePtr<SlotType>& One, const TUniquePtr<SlotType>& Two)
		{
			return Predicate(*One, *Two);
		});
		if (Owner)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	void Swap( int32 IndexA, int32 IndexB )
	{
		Children.Swap(IndexA, IndexB);
		if (Owner)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
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
		if (Owner && bChangesInvalidatePrepass)
		{ 
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}

		int32 Index = Children.Add(Child);

		if (Owner)
		{
			if (Child != SNullWidget::NullWidget)
			{
				Child->AssignParentWidget(Owner->AsShared());
			}
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
				Child->ConditionallyDetatchParentWidget(Owner);
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
				Child->ConditionallyDetatchParentWidget(Owner);
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
		if (Owner && bChangesInvalidatePrepass) 
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}

		Children.Insert(Child, Index);

		if (Owner)
		{
			if (Child != SNullWidget::NullWidget)
			{
				Child->AssignParentWidget(Owner->AsShared());
			}
		}
	}

	int32 Remove( const TSharedRef<ChildType>& Child )
	{
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(Owner);
		}

		return Children.Remove( Child );
	}

	void RemoveAt( int32 Index )
	{
		TSharedRef<SWidget> Child = GetChildAt(Index);
		if (Child != SNullWidget::NullWidget)
		{
			Child->ConditionallyDetatchParentWidget(Owner);
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
		if (Owner && bChangesInvalidatePrepass)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

	void Swap( int32 IndexA, int32 IndexB )
	{
		Children.Swap(IndexA, IndexB);
		if (Owner && bChangesInvalidatePrepass)
		{
			Owner->Invalidate(EInvalidateWidget::ChildOrder);
		}
	}

private:
	bool bChangesInvalidatePrepass;
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
	{ }

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