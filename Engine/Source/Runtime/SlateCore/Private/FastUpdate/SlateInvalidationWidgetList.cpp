// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/SlateInvalidationWidgetList.h"

#include "FastUpdate/SlateInvalidationRoot.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "FastUpdate/WidgetProxy.h"

#include "Algo/Unique.h"
#include "Layout/Children.h"
#include "Misc/StringBuilder.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Templates/ChooseClass.h"
#include "Templates/IsConst.h"
#include "Templates/Tuple.h"
#include "Types/ReflectionMetadata.h"

#include <limits>


DECLARE_CYCLE_STAT(TEXT("WidgetList ProcessInvalidation"), STAT_WidgetList_ProcessInvalidation, STATGROUP_Slate);

#define UE_SLATE_WITH_WIDGETLIST_UPDATEONLYWHATISNEEDED 0

#define UE_SLATE_VERIFY_REBUILDWIDGETDATA_ORDER 0
#define UE_SLATE_VERIFY_INVALID_INVALIDATIONHANDLE 0
#define UE_SLATE_VERIFY_REMOVED_WIDGET_ARE_NOT_INVALIDATED 0

#if UE_SLATE_VERIFY_REMOVED_WIDGET_ARE_NOT_INVALIDATED
uint16 GSlateInvalidationWidgetIndex_RemovedIndex = 0xffee;
#endif

// See FSlateInvalidationWidgetSortOrder::FSlateInvalidationWidgetSortOrder
int32 FSlateInvalidationWidgetList::FArguments::MaxPreferedElementsNum = (1 << 10) - 1;
int32 FSlateInvalidationWidgetList::FArguments::MaxSortOrderPaddingBetweenArray = (1 << 22) - 1;


FSlateInvalidationWidgetList::FSlateInvalidationWidgetList(FSlateInvalidationRootHandle InOwner, const FArguments& Args)
	: Owner(InOwner)
	, WidgetListConfig(Args)
{
	if (WidgetListConfig.PreferedElementsNum <= 1
		|| WidgetListConfig.PreferedElementsNum > FArguments::MaxPreferedElementsNum
		 || WidgetListConfig.SortOrderPaddingBetweenArray <= WidgetListConfig.PreferedElementsNum
		 || WidgetListConfig.SortOrderPaddingBetweenArray > FArguments::MaxSortOrderPaddingBetweenArray)
	{
		ensureMsgf(false, TEXT("The PreferedElementsNum or SortOrderPaddingBetweenArray have incorrect values. '%d,%d'. Reset to default value.")
			, WidgetListConfig.PreferedElementsNum
			, WidgetListConfig.SortOrderPaddingBetweenArray);
		const_cast<FArguments&>(WidgetListConfig).PreferedElementsNum = FArguments().PreferedElementsNum;
		const_cast<FArguments&>(WidgetListConfig).SortOrderPaddingBetweenArray = FArguments().SortOrderPaddingBetweenArray;
	}
}


FSlateInvalidationWidgetIndex FSlateInvalidationWidgetList::_BuildWidgetList_Recursive(TSharedRef<SWidget>& Widget, FSlateInvalidationWidgetIndex ParentIndex, IndexType& LastestIndex, bool bParentVisible, bool bParentVolatile)
{
	const bool bIsEmpty = IsEmpty();
	IndexType TemporaryIndex = LastestIndex;
	const FSlateInvalidationWidgetIndex NewIndex = EmplaceInsertAfter(LastestIndex, Widget);
	LastestIndex = NewIndex.ArrayIndex;

	FSlateInvalidationWidgetIndex LeafMostChildIndex = NewIndex;
	const EVisibility Visibility = Widget->GetVisibility();
	const bool bParentAndSelfVisible = bParentVisible && Visibility.IsVisible();

	{
		InvalidationWidgetType& WidgetProxy = (*this)[NewIndex];
		WidgetProxy.Index = NewIndex;
		WidgetProxy.ParentIndex = ParentIndex;
		WidgetProxy.LeafMostChildIndex = LeafMostChildIndex;
		WidgetProxy.Visibility = Visibility;
	}

#if WITH_SLATE_DEBUGGING
	if (WidgetListConfig.bAssignedWidgetIndex)
#endif
	{
		const FSlateInvalidationWidgetSortOrder SortIndex = { *this, NewIndex };
		Widget->SetFastPathProxyHandle(FWidgetProxyHandle{ Owner, NewIndex, SortIndex, GenerationNumber }, !bParentAndSelfVisible, bParentVolatile);
	}

	const bool bParentOrSelfVolatile = bParentVolatile || Widget->IsVolatile();

	// N.B. The SInvalidationBox needs a valid Proxy to decide if he's a root or not.
	//const bool bDoRecursion = ShouldDoRecursion(Widget);
	const bool bDoRecursion = !Widget->Advanced_IsInvalidationRoot() || bIsEmpty;
	if (bDoRecursion)
	{
		FChildren* Children = Widget->GetAllChildren();
		int32 NumChildren = Children->Num();
		for (int32 Index = 0; Index < NumChildren; ++Index)
		{
			TSharedRef<SWidget> NewWidget = Children->GetChildAt(Index);

			const bool bShouldAdd = ShouldBeAdded(NewWidget);
			if (bShouldAdd)
			{
				check(NewWidget->GetParentWidget() == Widget);
				LeafMostChildIndex = _BuildWidgetList_Recursive(NewWidget, NewIndex, LastestIndex, bParentAndSelfVisible, bParentOrSelfVolatile);
			}
		}

		InvalidationWidgetType& WidgetProxy = (*this)[NewIndex];
		WidgetProxy.LeafMostChildIndex = LeafMostChildIndex;
	}

	return LeafMostChildIndex;
}


void FSlateInvalidationWidgetList::BuildWidgetList(TSharedRef<SWidget> InRoot)
{
	Reset();
	Root = InRoot;
	FSlateInvalidationRoot*  InvalidationRoot = Owner.GetInvalidationRoot();
	if (InvalidationRoot)
	{
		GenerationNumber = InvalidationRoot->GetFastPathGenerationNumber();
	}

	const bool bShouldAdd = ShouldBeAdded(InRoot);
	if (bShouldAdd)
	{
		const TSharedPtr<SWidget> Parent = InRoot->GetParentWidget();
		const bool bParentVisible = Parent.IsValid() ? Parent->GetVisibility().IsVisible() : true;
		const bool bParentVolatile = false;
		IndexType LatestArrayIndex = FSlateInvalidationWidgetIndex::Invalid.ArrayIndex;
		_BuildWidgetList_Recursive(InRoot, FSlateInvalidationWidgetIndex::Invalid, LatestArrayIndex, bParentVisible, bParentVolatile);
	}
}


void FSlateInvalidationWidgetList::_RebuildWidgetListTree(TSharedRef<SWidget> Widget, int32 ChildAtIndex)
{
	const bool bShouldAddWidget = ShouldBeAdded(Widget);
	FChildren* ParentChildren = Widget->GetAllChildren();
	const FSlateInvalidationWidgetIndex WidgetIndex = Widget->GetProxyHandle().GetWidgetIndex();
	if (bShouldAddWidget && ChildAtIndex < ParentChildren->Num() && WidgetIndex != FSlateInvalidationWidgetIndex::Invalid)
	{
		// Since we are going to add item, the array may get invalidated. Do not use after _BuildWidgetList_Recursive
		ensure((*this)[WidgetIndex].GetWidget() == &Widget.Get());
		FSlateInvalidationWidgetIndex PreviousLeafIndex = (*this)[WidgetIndex].LeafMostChildIndex;
		FSlateInvalidationWidgetIndex NewLeafIndex = PreviousLeafIndex;
		IndexType LastestArrayIndex = PreviousLeafIndex.ArrayIndex;
		const bool bDoRecursion = ShouldDoRecursion(Widget);
		if (bDoRecursion)
		{
			const bool bParentVisible = Widget->IsFastPathVisible() && Widget->GetVisibility().IsVisible();
			const bool bParentVolatile = Widget->IsVolatileIndirectly() || Widget->IsVolatile();
			int32 NumChildren = ParentChildren->Num();
			for (int32 Index = ChildAtIndex; Index < NumChildren; ++Index)
			{
				TSharedRef<SWidget> ChildWidget = ParentChildren->GetChildAt(Index);
				const bool bShouldAddChild = ShouldBeAdded(ChildWidget);
				if (bShouldAddChild)
				{
					check(ChildWidget->GetParentWidget() == Widget);
					NewLeafIndex = _BuildWidgetList_Recursive(ChildWidget, WidgetIndex, LastestArrayIndex, bParentVisible, bParentVolatile);
				}
			}
		}

		if (NewLeafIndex != PreviousLeafIndex)
		{
			InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];
			InvalidationWidget.LeafMostChildIndex = NewLeafIndex;
			UpdateParentLeafIndex(InvalidationWidget, PreviousLeafIndex, NewLeafIndex);
		}
	}
}


//test if it's FSlateInvalidationWidgetIndex is valid and this == Widget.
//when we remove a widget we may not invalidate it's index and it may not point to the correct widget anymore
//That behavior is normal, if it occurred during ProcessChildOrderInvalidation.
void FSlateInvalidationWidgetList::ProcessChildOrderInvalidation(const TArray<TWeakPtr<SWidget>>& InvalidatedWidgets)
{
	SCOPE_CYCLE_COUNTER(STAT_WidgetList_ProcessInvalidation);

	FSlateInvalidationRoot* InvalidationRoot = Owner.GetInvalidationRoot();
	if (InvalidationRoot && InvalidationRoot->GetFastPathGenerationNumber() != GenerationNumber)
	{
		TSharedPtr<SWidget> RootPinned = GetRoot().Pin();
		if (ensure(RootPinned))
		{
			BuildWidgetList(RootPinned.ToSharedRef());
		}
		return;
	}

	if (InvalidatedWidgets.Num() > 0)
	{
		if (FirstIndex() == FSlateInvalidationWidgetIndex::Invalid)
		{
			ensureMsgf(false, TEXT("No tree were built and widgets were invalidated."));
			return;
		}

		FMemMark Mark(FMemStack::Get());
		using TInvalidateWidgetIndexType = TTuple<TSharedRef<SWidget>, FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder>;
		TArray<TInvalidateWidgetIndexType, TMemStackAllocator<>> InvalidatedWidgetIndexes;
		InvalidatedWidgetIndexes.Reserve(InvalidatedWidgets.Num());

#if UE_SLATE_VERIFY_INVALID_INVALIDATIONHANDLE
		TArray<TWeakPtr<SWidget>> VerifyWidgetInvalidationHandle;
		InvalidatedWidgetIndexes.Reserve(InvalidatedWidgets.Num());
#endif

		// Build Invalidation Indexes
		{
			for (const TWeakPtr<SWidget>& InvalidatedWidget : InvalidatedWidgets)
			{
				if (const TSharedPtr<SWidget> Widget = InvalidatedWidget.Pin())
				{
					const FSlateInvalidationWidgetIndex WidgetIndex = Widget->GetProxyHandle().GetWidgetIndex();
					if (IsValidIndex(WidgetIndex) && (*this)[WidgetIndex].GetWidget() == Widget.Get())
					{
						InvalidatedWidgetIndexes.Emplace(Widget.ToSharedRef(), WidgetIndex, FSlateInvalidationWidgetSortOrder{ *this, WidgetIndex });
					}
#if UE_SLATE_VERIFY_INVALID_INVALIDATIONHANDLE
					else
					{
						// This widget has requested an invalidation ChildOrder but it's index is not valid.
						//Confirm that it will have a valid index at the end of the algo.
						VerifyWidgetInvalidationHandle.Add(InvalidatedWidget);
						Widget->SetFastPathProxyHandle(FWidgetProxyHandle{ Owner, FSlateInvalidationWidgetIndex::Invalid, FSlateInvalidationWidgetSortOrder{}, GenerationNumber });
					}
#endif
				}
			}

			// Sort to invalidate them in the same order they would be if we were in slow path.
			InvalidatedWidgetIndexes.Sort([](const TInvalidateWidgetIndexType& A, const TInvalidateWidgetIndexType& B) {
				const FSlateInvalidationWidgetSortOrder OrderA = A.Get<2>();
				const FSlateInvalidationWidgetSortOrder OrderB = B.Get<2>();
				return OrderA < OrderB;
				});

			const int32 InvalidatedNum = Algo::Unique(InvalidatedWidgetIndexes);
			if (InvalidatedNum != InvalidatedWidgetIndexes.Num())
			{
				InvalidatedWidgetIndexes.RemoveAt(InvalidatedNum, InvalidatedWidgetIndexes.Num() - InvalidatedNum, false);
			}
		}

		struct FChildOrderInvalidationData
		{
			TSharedPtr<SWidget> Widget;
			FIndexRange Range;
			FSlateInvalidationWidgetIndex WhereToCut;
			int32 ChildAtToStartWith;
			bool bRemove;
			bool operator==(const FChildOrderInvalidationData& Other) const
			{
				return Other.Widget == Widget
					&& Other.bRemove == bRemove
					&& (bRemove ? Other.Range == Range : Other.WhereToCut == WhereToCut)
					&& Other.ChildAtToStartWith == ChildAtToStartWith;
			}
			FChildOrderInvalidationData(TSharedPtr<SWidget> InWidget, FIndexRange InRange, int32 InChildAtToStartWith)
				: Widget(InWidget)
				, Range(InRange)
				, WhereToCut(FSlateInvalidationWidgetIndex::Invalid)
				, ChildAtToStartWith(InChildAtToStartWith)
				, bRemove(true) {}
			FChildOrderInvalidationData(TSharedPtr<SWidget> InWidget, FSlateInvalidationWidgetIndex InWhereToCut, int32 InChildAtToStartWith)
				: Widget(InWidget)
				, Range()
				, WhereToCut(InWhereToCut)
				, ChildAtToStartWith(InChildAtToStartWith)
				, bRemove(false) {}
		};

		TArray<FChildOrderInvalidationData, TMemStackAllocator<>> RebuildWigetData;
		RebuildWigetData.Reserve(InvalidatedWidgetIndexes.Num());


		// Compute the InvalidationDatas
		{
			SCOPED_NAMED_EVENT(Slate_InvalidationList_ProcessCompute, FColorList::Blue);
			TArray<FIndexRange, TMemStackAllocator<>> RebuildWidgetRange;
			RebuildWidgetRange.Reserve(InvalidatedWidgetIndexes.Num());

			for (int32 InvalidatedIndex = 0; InvalidatedIndex < InvalidatedWidgetIndexes.Num(); ++InvalidatedIndex)
			{
				FSlateInvalidationWidgetIndex WidgetIndex = InvalidatedWidgetIndexes[InvalidatedIndex].Get<1>();

				// Is it already going to be invalidated by something else
				{
					const FSlateInvalidationWidgetSortOrder WidgetOrder = { *this, WidgetIndex };
					if (RebuildWidgetRange.ContainsByPredicate([WidgetOrder](const FIndexRange& Range) {
						return Range.Include(WidgetOrder);
						}))
					{
						continue;
					}
				}

				const InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];

				// If the parent is invalid, it's means that we are the root. Rebuild the full tree.
				if (InvalidationWidget.ParentIndex == FSlateInvalidationWidgetIndex::Invalid)
				{
					SWidget* WidgetPtr = InvalidationWidget.GetWidget();
					if (ensure(WidgetPtr))
					{
						ensure(GetRoot().Pin().Get() == WidgetPtr);
						UE_LOG(LogSlate, Warning, TEXT("Performance: A BuildTree() was requested by a ChildOrder invalidation."));
						BuildWidgetList(WidgetPtr->AsShared());
					}
					return;
				}

				TSharedRef<SWidget> WidgetRef = InvalidatedWidgetIndexes[InvalidatedIndex].Get<0>();
				ensureMsgf(WidgetRef->GetProxyHandle().GetWidgetIndex() == WidgetIndex, TEXT("The widget index doesn't match the index in the InvalidationWidgetList"));

				// Find all the InvalidationWidget children
				{
					// Was added, but it should not be there anymore
					if (!ShouldBeAdded(WidgetRef))
					{
						const FIndexRange Range = { *this, WidgetIndex, InvalidationWidget.LeafMostChildIndex };
						RebuildWidgetRange.Add(Range);
						RebuildWigetData.Emplace(TSharedPtr<SWidget>(), Range, INDEX_NONE);
					}
					// If it is not supposed to had child, but had some, them remove them
					else if (!ShouldDoRecursion(WidgetRef))
					{
						if (WidgetIndex != InvalidationWidget.LeafMostChildIndex)
						{
							const FIndexRange Range = { *this, IncrementIndex(WidgetIndex), InvalidationWidget.LeafMostChildIndex };
							RebuildWidgetRange.Add(Range);
							RebuildWigetData.Emplace(TSharedPtr<SWidget>(), Range, INDEX_NONE);
						}
					}
					// Find the difference between list and the reality (if it used to have at least 1 child)
					else if (WidgetIndex != InvalidationWidget.LeafMostChildIndex)
					{
#if UE_SLATE_WITH_WIDGETLIST_UPDATEONLYWHATISNEEDED
						// Find all it's previous children
						FMemMark Mark2(FMemStack::Get());
						TArray<SWidget*, TMemStackAllocator<>> PreviousChildrenWidget;
						_FindChildren(WidgetIndex, PreviousChildrenWidget);

						FChildren* InvalidatedChildren = WidgetRef->GetAllChildren();
						check(InvalidatedChildren);

						/**
						 ****** ****** ****** ****** ******
						 * todo @pat improve with a loop
						 * As a fist step, invalidate everything from this point. Could be a loop and invalidate what need to be invalidated before move the other.
						 * We have to see if that case occurred in data before doing the code for it.
						 ****** ****** ****** ****** ******
						*/

						// Find where it starts to get different
						FSlateInvalidationWidgetIndex IndexWhereToStart = IncrementIndex(WidgetIndex);
						FSlateInvalidationWidgetIndex IndexWhereToEnd = InvalidationWidget.LeafMostChildIndex;
						int32 InvalidatedChildrenIndex = 0;

						const int32 InvalidatedChildrenNum = InvalidatedChildren->Num();
						const int32 PreviousChildrenNum = PreviousChildrenWidget.Num();
						int32 PreviousIndex = 0;
						for (; InvalidatedChildrenIndex < InvalidatedChildrenNum && PreviousIndex < PreviousChildrenNum; ++InvalidatedChildrenIndex)
						{
							TSharedRef<SWidget> NewWidget = InvalidatedChildren->GetChildAt(InvalidatedChildrenIndex);
							if (ShouldBeAdded(NewWidget))
							{
								if (&NewWidget.Get() != PreviousChildrenWidget[PreviousIndex])
								{
									break;
								}
								IndexWhereToStart = IncrementIndex((*this)[IndexWhereToStart].LeafMostChildIndex);
								check(IsValidIndex(IndexWhereToStart));

								++PreviousIndex;
							}
						}

						if (InvalidatedChildrenIndex >= InvalidatedChildrenNum && PreviousIndex >= PreviousChildrenNum)
						{
							// The widget was invalidated but nothing changed. This could be normal if a widget was removed, then re-added.
						}
						else if (PreviousIndex >= PreviousChildrenNum)
						{
							// Nothing to remove, but need to add some. We want to break the array.
							RebuildWigetData.Emplace(WidgetRef, InvalidationWidget.LeafMostChildIndex, InvalidatedChildrenIndex);
						}
						else
						{
							const FIndexRange Range = { *this, IndexWhereToStart, IndexWhereToEnd };
							check(Range.OrderMin <= Range.OrderMax);
							RebuildWidgetRange.Add(Range);
							RebuildWigetData.Emplace(WidgetRef, Range, InvalidatedChildrenIndex);
						}
#else
						// Remove every children and rebuld the widget.
						const FSlateInvalidationWidgetIndex IndexWhereToStart = IncrementIndex(WidgetIndex);
						const FSlateInvalidationWidgetIndex IndexWhereToEnd = InvalidationWidget.LeafMostChildIndex;
						const FIndexRange Range = { *this, IndexWhereToStart, IndexWhereToEnd };
						RebuildWidgetRange.Add(Range);
						RebuildWigetData.Emplace(WidgetRef, Range, 0);
#endif
					}
					// There was not child, but maybe it has some now
					else
					{
						FChildren* InvalidatedChildren = WidgetRef->GetAllChildren();
						check(InvalidatedChildren);
						if (InvalidatedChildren->Num() > 0)
						{
							// Nothing to remove, but need to add new. We want to break the array.
							RebuildWigetData.Emplace(WidgetRef, InvalidationWidget.LeafMostChildIndex, 0);
						}
					}
				}
			}
		}

#if UE_SLATE_VERIFY_REBUILDWIDGETDATA_ORDER
		// The array was sorted, but we may have invalidated a parent to only add at the end and the sub tree before is also invalidated.
		//A (B(1,2), C(5,6)) => A (B(1,2,3), C(5, 6) D(8))
		//A and B are invalidated. They were sorted (A, B). Now we know that D and B process. B need to be process after.
		//This should already work (because we are doing the reverse operation). Lets confirm.
		{
			FMemMark Mark2(FMemStack::Get());
			TArray<FChildOrderInvalidationData, TMemStackAllocator<>> SortedRebuildWigetData = RebuildWigetData;
			FSlateInvalidationWidgetList* Self = this;
			SortedRebuildWigetData.Sort([Self](const FChildOrderInvalidationData& A, const FChildOrderInvalidationData& B) -> bool
				{
					const FSlateInvalidationWidgetIndex AIndex = A.bRemove ? A.Range.InclusiveMin : A.WhereToCut;
					const FSlateInvalidationWidgetIndex BIndex = B.bRemove ? B.Range.InclusiveMin : B.WhereToCut;
					return FSlateInvalidationWidgetSortOrder(*Self, AIndex) < FSlateInvalidationWidgetSortOrder(*Self, BIndex);
				});

			// Is the sorted list in the same order
			ensureAlways(SortedRebuildWigetData == RebuildWigetData);
		}
#endif //UE_SLATE_VERIFY_REBUILDWIDGETDATA_ORDER

		{
			SCOPED_NAMED_EVENT(Slate_InvalidationList_ProcessRemove, FColorList::Blue);
			for (int32 Index = RebuildWigetData.Num() - 1; Index >= 0; --Index)
			{
				const FChildOrderInvalidationData& RebuildData = RebuildWigetData[Index];
				if (RebuildData.bRemove)
				{
					_RemoveRangeFromSameParent(RebuildData.Range);
				}
				else
				{
					CutArray(RebuildData.WhereToCut);
				}
			}
		}

		{
			SCOPED_NAMED_EVENT(Slate_InvalidationList_ProcessRebuild, FColorList::Blue);
			for (const FChildOrderInvalidationData& RebuildData : RebuildWigetData)
			{
				if (RebuildData.Widget)
				{
#if UE_SLATE_VERIFY_REMOVED_WIDGET_ARE_NOT_INVALIDATED
					FSlateInvalidationWidgetIndex SlateInvalidationWidgetIndexRemoved = { GSlateInvalidationWidgetIndex_RemovedIndex, GSlateInvalidationWidgetIndex_RemovedIndex };
					ensure(RebuildData.Widget->GetProxyHandle().GetWidgetIndex() != SlateInvalidationWidgetIndexRemoved);
#endif
					_RebuildWidgetListTree(RebuildData.Widget.ToSharedRef(), RebuildData.ChildAtToStartWith);
				}
			}
		}


#if UE_SLATE_VERIFY_INVALID_INVALIDATIONHANDLE
		{
			for (TWeakPtr<SWidget>& WidgetInvalidated : VerifyWidgetInvalidationHandle)
			{
				if (TSharedPtr<SWidget> Widget = WidgetInvalidated.Pin())
				{
					const FSlateInvalidationWidgetIndex WidgetIndex = Widget->GetProxyHandle().GetWidgetIndex();
					ensureMsgf(WidgetIndex != FSlateInvalidationWidgetIndex::Invalid, TEXT("The widget '%s' requested a ChildOrder but didn't have a valid index and was not rebuilt by something else.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget.Get()));
				}
			}
		}
#endif
	}
}


namespace
{
	template<typename TSlateInvalidationWidgetList, typename Predicate>
	void ForEachChildren(TSlateInvalidationWidgetList& Self, const typename TSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget, FSlateInvalidationWidgetIndex WidgetIndex, Predicate InPredicate)
	{
		using SlateInvalidationWidgetType = typename TChooseClass<TIsConst<TSlateInvalidationWidgetList>::Value, const typename TSlateInvalidationWidgetList::InvalidationWidgetType, typename TSlateInvalidationWidgetList::InvalidationWidgetType>::Result;
		if (InvalidationWidget.LeafMostChildIndex != WidgetIndex)
		{
			FSlateInvalidationWidgetIndex CurrentWidgetIndex = Self.IncrementIndex(WidgetIndex);
			while (true)
			{
				SlateInvalidationWidgetType& CurrentInvalidationWidget = Self[CurrentWidgetIndex];
				InPredicate(CurrentInvalidationWidget);
				CurrentWidgetIndex = CurrentInvalidationWidget.LeafMostChildIndex;
				if (InvalidationWidget.LeafMostChildIndex == CurrentWidgetIndex)
				{
					break;
				}
				CurrentWidgetIndex = Self.IncrementIndex(CurrentWidgetIndex);
				if (InvalidationWidget.LeafMostChildIndex == CurrentWidgetIndex)
				{
					InPredicate(Self[CurrentWidgetIndex]);
					break;
				}
			}
		}
	}
}


void FSlateInvalidationWidgetList::_FindChildren(FSlateInvalidationWidgetIndex WidgetIndex, TArray<SWidget*, TMemStackAllocator<>>& Widgets) const
{
	Widgets.Reserve(16);
	const InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];
	ForEachChildren(*this, InvalidationWidget, WidgetIndex, [&Widgets](const InvalidationWidgetType& ChildWidget)
		{
			Widgets.Add(ChildWidget.GetWidget());
		});
}


//FSlateInvalidationWidgetIndex FSlateInvalidationWidgetList::_FindPreviousSibling(FSlateInvalidationWidgetIndex WidgetIndex) const
//{
//	FSlateInvalidationWidgetIndex Result = FSlateInvalidationWidgetIndex::Invalid;
//	const InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];
//	if (InvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
//	{
//		const FSlateInvalidationWidgetIndex PreviousWidgetIndex = DecrementIndex(WidgetIndex);
//		const InvalidationWidgetType& PreviousInvalidationWidget = (*this)[WidgetIndex];
//		if (PreviousInvalidationWidget.ParentIndex == InvalidationWidget.ParentIndex)
//		{
//			Result = PreviousWidgetIndex;
//		}
//		else
//		{
//			Result = PreviousInvalidationWidget.ParentIndex;
//		}
//	}
//	return  Result;
//}


FSlateInvalidationWidgetIndex FSlateInvalidationWidgetList::IncrementIndex(FSlateInvalidationWidgetIndex Index) const
{
	check(Data.IsValidIndex(Index.ArrayIndex));
	++Index.ElementIndex;
	if (Index.ElementIndex >= Data[Index.ArrayIndex].ElementList.Num())
	{
		if (Data[Index.ArrayIndex].NextArrayIndex == INDEX_NONE)
		{
			return FSlateInvalidationWidgetIndex::Invalid;
		}
		check(Data[Index.ArrayIndex].NextArrayIndex < FSlateInvalidationWidgetIndex::Invalid.ArrayIndex);
		Index.ArrayIndex = (IndexType)Data[Index.ArrayIndex].NextArrayIndex;
		Index.ElementIndex = Data[Index.ArrayIndex].StartIndex;
	}
	return Index;
}


FSlateInvalidationWidgetIndex FSlateInvalidationWidgetList::DecrementIndex(FSlateInvalidationWidgetIndex Index) const
{
	check(Data.IsValidIndex(Index.ArrayIndex));
	if (Index.ElementIndex == Data[Index.ArrayIndex].StartIndex)
	{
		if (Data[Index.ArrayIndex].PreviousArrayIndex == INDEX_NONE)
		{
			return FSlateInvalidationWidgetIndex::Invalid;
		}
		check(Data[Index.ArrayIndex].PreviousArrayIndex < FSlateInvalidationWidgetIndex::Invalid.ArrayIndex);
		Index.ArrayIndex = (IndexType)Data[Index.ArrayIndex].PreviousArrayIndex;
		check(Data[Index.ArrayIndex].ElementList.Num() > 0);
		Index.ElementIndex = Data[Index.ArrayIndex].ElementList.Num() - 1;
	}
	else
	{
		--Index.ElementIndex;
	}
	return Index;
}


void FSlateInvalidationWidgetList::Empty()
{
	Data.Empty();
	Root.Reset();
	FirstArrayIndex = INDEX_NONE;
	LastArrayIndex = INDEX_NONE;
}


void FSlateInvalidationWidgetList::Reset()
{
	Data.Reset();
	Root.Reset();
	FirstArrayIndex = INDEX_NONE;
	LastArrayIndex = INDEX_NONE;
	GenerationNumber = INDEX_NONE;
}


FSlateInvalidationWidgetList::IndexType FSlateInvalidationWidgetList::AddArrayNodeIfNeeded(bool bReserveElementList)
{
	if (LastArrayIndex == INDEX_NONE || Data[LastArrayIndex].ElementList.Num() + 1 > WidgetListConfig.PreferedElementsNum)
	{
		if (Data.Num() + 1 == FSlateInvalidationWidgetIndex::Invalid.ArrayIndex)
		{
			ensure(false);
			return LastArrayIndex;
		}
		const int32 Index = Data.Add(FArrayNode());
		check(Index < std::numeric_limits<IndexType>::max());
		if (bReserveElementList)
		{
			Data[Index].ElementList.Reserve(WidgetListConfig.PreferedElementsNum);
		}

		if (LastArrayIndex != INDEX_NONE)
		{
			Data[LastArrayIndex].NextArrayIndex = Index;
			Data[Index].SortOrder = Data[LastArrayIndex].SortOrder + WidgetListConfig.SortOrderPaddingBetweenArray;
		}
		Data[Index].PreviousArrayIndex = LastArrayIndex;

		LastArrayIndex = Index;
		if (FirstArrayIndex == INDEX_NONE)
		{
			FirstArrayIndex = LastArrayIndex;
		}
	}
	return LastArrayIndex;
}


FSlateInvalidationWidgetList::IndexType FSlateInvalidationWidgetList::InsertArrayNodeIfNeeded(IndexType AfterArrayIndex, bool bReserveElementList)
{
	if (AfterArrayIndex == FSlateInvalidationWidgetIndex::Invalid.ArrayIndex)
	{
		return AddArrayNodeIfNeeded(bReserveElementList);
	}
	else if (Data[AfterArrayIndex].ElementList.Num() + 1 > WidgetListConfig.PreferedElementsNum)
	{
		return InsertDataNodeAfter(AfterArrayIndex, bReserveElementList);
	}
	return AfterArrayIndex;
}


void FSlateInvalidationWidgetList::RebuildOrderIndex(IndexType StartFrom)
{
	check(StartFrom != INDEX_NONE);
	check(WidgetListConfig.PreferedElementsNum < WidgetListConfig.MaxPreferedElementsNum);

	FSlateInvalidationWidgetList* Self = this;
	auto SetValue = [Self](IndexType ArrayIndex)
		{
			ElementListType& ElementList = Self->Data[ArrayIndex].ElementList;
			for (int32 ElementIndex = Self->Data[ArrayIndex].StartIndex; ElementIndex < ElementList.Num(); ++ElementIndex)
			{
				FSlateInvalidationWidgetIndex WidgetIndex = { ArrayIndex, (IndexType)ElementIndex };
				FSlateInvalidationWidgetSortOrder SortOrder = { *Self, WidgetIndex };
				ElementList[ElementIndex].GetWidget()->SetFastPathSortOrder(SortOrder);
			}
		};

	for (int32 CurrentIndex = StartFrom; CurrentIndex != INDEX_NONE; CurrentIndex = Data[CurrentIndex].NextArrayIndex)
	{
		const int32 PreviousIndex = Data[CurrentIndex].PreviousArrayIndex;
		const int32 NextIndex = Data[CurrentIndex].NextArrayIndex;

		if (PreviousIndex == INDEX_NONE)
		{
			Data[CurrentIndex].SortOrder = 0;
			SetValue((IndexType)CurrentIndex);
		}
		else if (NextIndex == INDEX_NONE)
		{
			Data[CurrentIndex].SortOrder = Data[PreviousIndex].SortOrder + WidgetListConfig.SortOrderPaddingBetweenArray;
			SetValue((IndexType)CurrentIndex);
			break;
		}
		else
		{
			const int32 PreviousMinSortOrder = Data[PreviousIndex].SortOrder;
			const int32 PreviousMaxSortOrder = Data[PreviousIndex].SortOrder + WidgetListConfig.PreferedElementsNum;
			const int32 NextMinSortOrder = Data[NextIndex].SortOrder;
			const int32 CurrentSortOrder = Data[CurrentIndex].SortOrder;

			// Is everything already good
			if (PreviousMaxSortOrder < CurrentSortOrder && CurrentSortOrder + WidgetListConfig.PreferedElementsNum < NextMinSortOrder)
			{
				break;
			}
			// Would the normal padding be valid
			else if (PreviousMinSortOrder + WidgetListConfig.SortOrderPaddingBetweenArray < NextMinSortOrder)
			{
				Data[CurrentIndex].SortOrder = PreviousMaxSortOrder + WidgetListConfig.SortOrderPaddingBetweenArray;
				SetValue((IndexType)CurrentIndex);
			}
			// Would padding by half would be valid
			else if (NextMinSortOrder > PreviousMaxSortOrder && NextMinSortOrder - PreviousMaxSortOrder >= WidgetListConfig.PreferedElementsNum)
			{
				// We prefer to keep space of the exact amount in PreferedElementsNum in front Previous sort order and in the back Next sort order.
				//That way we potently need less reorder in the future.
				const int32 NumSpacesAvailable = (NextMinSortOrder - PreviousMaxSortOrder) / WidgetListConfig.PreferedElementsNum;
				const int32 NewCurrentOrderIndex = PreviousMaxSortOrder + (WidgetListConfig.PreferedElementsNum * (NumSpacesAvailable/2));
				check(PreviousMaxSortOrder <= NewCurrentOrderIndex && NewCurrentOrderIndex + WidgetListConfig.PreferedElementsNum <= NextMinSortOrder);
				Data[CurrentIndex].SortOrder = NewCurrentOrderIndex;
				SetValue((IndexType)CurrentIndex);
			}
			// Worst case, need to also rebuild the next array
			else
			{
				Data[CurrentIndex].SortOrder = PreviousMaxSortOrder + FMath::Min(WidgetListConfig.PreferedElementsNum * 2, WidgetListConfig.SortOrderPaddingBetweenArray);
				SetValue((IndexType)CurrentIndex);
			}
		}
		ensureMsgf(Data[CurrentIndex].SortOrder <= WidgetListConfig.MaxSortOrderPaddingBetweenArray
			, TEXT("The order index '%d' is too big to be contained inside the WidgetSortIndex. The Widget order will not be valid.")
			, Data[CurrentIndex].SortOrder); // See FSlateInvalidationWidgetSortOrder
	}
}


FSlateInvalidationWidgetList::IndexType FSlateInvalidationWidgetList::InsertDataNodeAfter(IndexType AfterIndex, bool bReserveElementList)
{
	if (FirstArrayIndex == INDEX_NONE)
	{
		check(AfterIndex == INDEX_NONE);
		check(LastArrayIndex == INDEX_NONE);
		AddArrayNodeIfNeeded(bReserveElementList);
		return (IndexType)LastArrayIndex;
	}
	else
	{
		check(AfterIndex != INDEX_NONE);

		if (Data.Num() + 1 == FSlateInvalidationWidgetIndex::Invalid.ArrayIndex)
		{
			ensure(false);
			return LastArrayIndex;
		}

		const int32 NewIndex = Data.Add(FArrayNode());
		check(NewIndex < std::numeric_limits<IndexType>::max());
		if (bReserveElementList)
		{
			Data[NewIndex].ElementList.Reserve(WidgetListConfig.PreferedElementsNum);
		}

		FArrayNode& AfterArrayNode = Data[AfterIndex];
		if (AfterArrayNode.NextArrayIndex != INDEX_NONE)
		{
			Data[AfterArrayNode.NextArrayIndex].PreviousArrayIndex = NewIndex;
			Data[NewIndex].NextArrayIndex = AfterArrayNode.NextArrayIndex;

			AfterArrayNode.NextArrayIndex = NewIndex;
			Data[NewIndex].PreviousArrayIndex = AfterIndex;

			if (LastArrayIndex == AfterIndex)
			{
				LastArrayIndex = NewIndex;
			}

			RebuildOrderIndex(NewIndex);
		}
		else
		{
			check(LastArrayIndex == AfterIndex);
			LastArrayIndex = NewIndex;
			Data[NewIndex].PreviousArrayIndex = AfterIndex;
			Data[AfterIndex].NextArrayIndex = NewIndex;
			Data[NewIndex].SortOrder = Data[AfterIndex].SortOrder + WidgetListConfig.SortOrderPaddingBetweenArray;
		}

		return (IndexType)NewIndex;
	}
}


void FSlateInvalidationWidgetList::RemoveDataNode(IndexType Index)
{
	check(Index != INDEX_NONE && Index != std::numeric_limits<IndexType>::max());
	FArrayNode& ArrayNode = Data[Index];
	if (ArrayNode.PreviousArrayIndex != INDEX_NONE)
	{
		Data[ArrayNode.PreviousArrayIndex].NextArrayIndex = ArrayNode.NextArrayIndex;
	}
	else
	{
		FirstArrayIndex = ArrayNode.NextArrayIndex;
	}

	if (ArrayNode.NextArrayIndex != INDEX_NONE)
	{
		Data[ArrayNode.NextArrayIndex].PreviousArrayIndex = ArrayNode.PreviousArrayIndex;
	}
	else
	{
		LastArrayIndex = ArrayNode.PreviousArrayIndex;
	}
	ArrayNode.ElementList.Empty();
	Data.RemoveAt(Index);

	// No need to rebuild the order when we remove.
	//OrderIndex is incremental and only use to sort.
	//RebuildOrderIndex();

	check(FirstArrayIndex != Index);
	check(LastArrayIndex != Index);
	if (Data.Num() == 0)
	{
		check(LastArrayIndex == INDEX_NONE && FirstArrayIndex == INDEX_NONE);
	}
	else
	{
		check(FirstArrayIndex != INDEX_NONE && LastArrayIndex != INDEX_NONE);
	}
}


void FSlateInvalidationWidgetList::UpdateParentLeafIndex(const InvalidationWidgetType& NewInvalidationWidget, FSlateInvalidationWidgetIndex OldWidgetIndex, FSlateInvalidationWidgetIndex NewWidgetIndex)
{
	if (NewInvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
	{
		InvalidationWidgetType* ParentInvalidationWidget = &(*this)[NewInvalidationWidget.ParentIndex];
		while (ParentInvalidationWidget->LeafMostChildIndex == OldWidgetIndex)
		{
			ParentInvalidationWidget->LeafMostChildIndex = NewWidgetIndex;
			if (ParentInvalidationWidget->ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
			{
				ParentInvalidationWidget = &(*this)[ParentInvalidationWidget->ParentIndex];
			}
		}
	}
}


/**
 * To remove child from the same parent or all it's children.
 * A ( B (C,D), E (F,G) )
 * Can use to remove range (B,D) or (C,C) or (C,D) or (E, G) or (F,G) or (B,G).
 * Cannot use (B,E) or (B,C) or (B,F)
 */
void FSlateInvalidationWidgetList::_RemoveRangeFromSameParent(const FIndexRange Range)
{
	// Fix up Parent's LeafIndex if they are in Range
	{
		//N.B. The algo doesn't support cross family removal. We do not need to worry about fixing the ParentIndex.
		//(i)	There is no other child. Set Parent's LeafIndex to itself (recursive) [remove (F,G), leaf of E=E and leaf of A=E]
		//(ii)	The Parent's LeafIndex is already set properly [remove (B,C), leaf of A is already G]
		//(iii)	Parent's LeafIndex should be set to the previous sibling' leaf (recursive) [remove (G,G), leaf of E=F, A=F]
		const InvalidationWidgetType& InvalidationWidgetStart = (*this)[Range.InclusiveMin];
		const InvalidationWidgetType& InvalidationWidgetEnd = (*this)[Range.InclusiveMax];

		// Parent index could be invalid if there is only one widget and we are removing it
		if (InvalidationWidgetStart.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
		{
			// Is the parent's leaf being removed
			const InvalidationWidgetType& InvalidationWidgetParentStart = (*this)[InvalidationWidgetStart.ParentIndex];
			if (Range.Include(FSlateInvalidationWidgetSortOrder{ *this, InvalidationWidgetParentStart.LeafMostChildIndex }))
			{
				// _RemoveRangeFromSameParent doesn't support cross family removal
				check(Range.Include(FSlateInvalidationWidgetSortOrder{ *this, (*this)[InvalidationWidgetEnd.ParentIndex].LeafMostChildIndex }));
				const FSlateInvalidationWidgetIndex PreviousWidget = DecrementIndex(Range.InclusiveMin);
				UpdateParentLeafIndex(InvalidationWidgetStart, InvalidationWidgetParentStart.LeafMostChildIndex, PreviousWidget);
			}
		}
	}

	//N.B. Theres is no parent/left relation in the array.
	//ie.		1234 5678 90ab	(they have a size of 4, we cut at 2)
	//(i)		123x xxxx x0ab => 123 x0ab			=> no cut, remove 4, remove 5-8, set StartIndex to 0
	//(ii)		123x xxxx xxxb => 123 b				=> cut, remove 4, remove 5-8, remove 9-b (b was moved)
	//(iii)		12xx 5478 90ab => 12 5478 90ab		=> cut, remove 3-4
	//(iv)		1234 x678 90ab => 1234 x678 90ab	=> no cut, set StartIndex to 6
	//(v)		1234 xxx8 90ab => 1234 8 90ab		=> cut, remove 5-8, (8 was moved)
	//(vi)		1234 5xx8 90ab => 1234 5 8 90ab		=> cut, remove 6-8, (8 was moved)

	const int32 NumberElementLeft = Data[Range.InclusiveMax.ArrayIndex].ElementList.Num()
		//- Data[Range.InclusiveMax.ArrayIndex].StartIndex
		- Range.InclusiveMax.ElementIndex
		- 1;
	const bool bRangeIsInSameElementArray = Range.InclusiveMin.ArrayIndex == Range.InclusiveMax.ArrayIndex;
	const bool bShouldCutArray = NumberElementLeft < WidgetListConfig.NumberElementsLeftBeforeSplitting
		|| (bRangeIsInSameElementArray && Data[Range.InclusiveMin.ArrayIndex].StartIndex != Range.InclusiveMin.ElementIndex);
	if (bShouldCutArray)
	{
		_CutArray(Range.InclusiveMax);
	}

	// Destroy/Remove the data that is not needed anymore
	{
		FSlateInvalidationWidgetList* Self = this;
		auto SetFakeInvalidatWidgetHandle = [Self](IndexType ArrayIndex, int32 StartIndex, int32 Num)
		{
#if UE_SLATE_VERIFY_REMOVED_WIDGET_ARE_NOT_INVALIDATED
			const FSlateInvalidationWidgetIndex SlateInvalidationWidgetIndexRemoved = { GSlateInvalidationWidgetIndex_RemovedIndex, GSlateInvalidationWidgetIndex_RemovedIndex };
			for (int32 ElementIndex = StartIndex; ElementIndex < Num; ++ElementIndex)
			{
				InvalidationWidgetType& InvalidationWidget = Self->Data[ArrayIndex].ElementList[ElementIndex];
				if (SWidget* Widget = InvalidationWidget.GetWidget())
				{
					Widget->SetFastPathProxyHandle(FWidgetProxyHandle{ Self->Owner, SlateInvalidationWidgetIndexRemoved, FSlateInvalidationWidgetSortOrder(), GenerationNumber });
				}
			}
#endif
		};
		auto ResetInvalidationWidget = [Self](IndexType ArrayIndex, int32 StartIndex, int32 Num)
		{
			ElementListType& ResetElementList = Self->Data[ArrayIndex].ElementList;
			for (int32 ElementIndex = StartIndex; ElementIndex < Num; ++ElementIndex)
			{
				ResetElementList[ElementIndex].ResetWidget();
			}
		};

		auto RemoveDataNodeIfNeeded = [Self](int32 ArrayIndex)
		{
			if (Self->Data[ArrayIndex].StartIndex >= Self->Data[ArrayIndex].ElementList.Num())
			{
				Self->RemoveDataNode(ArrayIndex);
			}
		};

		// Remove the other arrays that are between min and max	(ie. i, ii)
		//if (Range.InclusiveMax.ArrayIndex - Range.InclusiveMin.ArrayIndex > 1)
		if (!bRangeIsInSameElementArray)
		{
			const IndexType BeginArrayIndex = (IndexType)Data[Range.InclusiveMin.ArrayIndex].NextArrayIndex;
			const IndexType EndArrayIndex = (IndexType)Data[Range.InclusiveMax.ArrayIndex].PreviousArrayIndex;

			if (BeginArrayIndex != Range.InclusiveMax.ArrayIndex)
			{
				IndexType NextToRemove = BeginArrayIndex;
				IndexType CurrentArrayIndex = NextToRemove;
				do
				{
					SetFakeInvalidatWidgetHandle(CurrentArrayIndex, Data[CurrentArrayIndex].StartIndex, Data[CurrentArrayIndex].ElementList.Num());
					CurrentArrayIndex = NextToRemove;
					NextToRemove = (IndexType)Data[CurrentArrayIndex].NextArrayIndex;
					RemoveDataNode(CurrentArrayIndex);
				} while (CurrentArrayIndex != EndArrayIndex);
			}
		}

		// Remove the start of the Max array
		if (bShouldCutArray && !bRangeIsInSameElementArray)
		{
			// The valid data in the array was moved (ie. ii)
			SetFakeInvalidatWidgetHandle(Range.InclusiveMax.ArrayIndex, Data[Range.InclusiveMax.ArrayIndex].StartIndex, Range.InclusiveMax.ElementIndex + 1);
			RemoveDataNode(Range.InclusiveMax.ArrayIndex);
		}
		else if (!bRangeIsInSameElementArray)
		{
			// Set StartIndex (ie. i)
			check(Range.InclusiveMin.ArrayIndex != Range.InclusiveMax.ArrayIndex);

			SetFakeInvalidatWidgetHandle(Range.InclusiveMax.ArrayIndex, Data[Range.InclusiveMax.ArrayIndex].StartIndex, Range.InclusiveMax.ElementIndex + 1);
			ResetInvalidationWidget(Range.InclusiveMax.ArrayIndex, Data[Range.InclusiveMax.ArrayIndex].StartIndex, Range.InclusiveMax.ElementIndex + 1);
			Data[Range.InclusiveMax.ArrayIndex].StartIndex = Range.InclusiveMax.ElementIndex + 1;
			RemoveDataNodeIfNeeded(Range.InclusiveMax.ArrayIndex);
		}

		// Remove what is left of the Min array
		if (bShouldCutArray || !bRangeIsInSameElementArray)
		{
			// RemoveAt Min to Num of the array. (ie. i, ii, iii, v, vi)
			ElementListType& RemoveElementList = Data[Range.InclusiveMin.ArrayIndex].ElementList;
			if (bRangeIsInSameElementArray)
			{
				SetFakeInvalidatWidgetHandle(Range.InclusiveMin.ArrayIndex, Range.InclusiveMin.ElementIndex, Range.InclusiveMax.ElementIndex);
			}
			else
			{
				SetFakeInvalidatWidgetHandle(Range.InclusiveMin.ArrayIndex, Range.InclusiveMin.ElementIndex, RemoveElementList.Num());
			}

			const int32 RemoveArrayAt = Range.InclusiveMin.ElementIndex;
			RemoveElementList.RemoveAt(RemoveArrayAt, RemoveElementList.Num() - RemoveArrayAt, true);
			RemoveDataNodeIfNeeded(Range.InclusiveMin.ArrayIndex);
		}
		else
		{
			// Set StartIndex (ie. iv)
			check(Range.InclusiveMin.ArrayIndex == Range.InclusiveMax.ArrayIndex);
			check(Range.InclusiveMin.ElementIndex == Data[Range.InclusiveMin.ArrayIndex].StartIndex);

			SetFakeInvalidatWidgetHandle(Range.InclusiveMin.ArrayIndex, Range.InclusiveMin.ElementIndex, Range.InclusiveMax.ElementIndex + 1);
			ResetInvalidationWidget(Range.InclusiveMin.ArrayIndex, Range.InclusiveMin.ElementIndex, Range.InclusiveMax.ElementIndex + 1);
			Data[Range.InclusiveMin.ArrayIndex].StartIndex = Range.InclusiveMax.ElementIndex + 1;
			RemoveDataNodeIfNeeded(Range.InclusiveMin.ArrayIndex);
		}
	}
}


void FSlateInvalidationWidgetList::CutArray(const FSlateInvalidationWidgetIndex WhereToCut)
{
	int32 OldElementIndexStart = _CutArray(WhereToCut);

	// Remove the old data that is now moved to the new array
	if (OldElementIndexStart != INDEX_NONE)
	{
		ElementListType& RemoveElementList = Data[WhereToCut.ArrayIndex].ElementList;
		RemoveElementList.RemoveAt(OldElementIndexStart, RemoveElementList.Num() - OldElementIndexStart, true);
		if (RemoveElementList.Num() == 0)
		{
			RemoveDataNode(WhereToCut.ArrayIndex);
		}
	}
}


int32 FSlateInvalidationWidgetList::_CutArray(const FSlateInvalidationWidgetIndex WhereToCut)
{
	SCOPED_NAMED_EVENT(Slate_InvalidationList_CutArray, FColor::Blue);

	// Should we cut the array and move the data to another array
	// N.B. We can cut/move anywhere. Cross family may occur. Fix up everything.
	if (WhereToCut.ElementIndex < Data[WhereToCut.ArrayIndex].ElementList.Num() - 1)
	{
		//From where to where we are moving the item
		const IndexType OldArrayIndex = WhereToCut.ArrayIndex;
		const IndexType OldElementIndexStart = WhereToCut.ElementIndex + 1;
		const IndexType OldElementIndexEnd = Data[WhereToCut.ArrayIndex].ElementList.Num();
		const IndexType NewArrayIndex = InsertDataNodeAfter(OldArrayIndex, false);
		const int32 NewExpectedElementArraySize = OldElementIndexEnd - OldElementIndexStart;
		Data[NewArrayIndex].ElementList.Reserve(NewExpectedElementArraySize);

		const FIndexRange OldRange = { *this, FSlateInvalidationWidgetIndex{ OldArrayIndex, OldElementIndexStart },  FSlateInvalidationWidgetIndex{ OldArrayIndex, (IndexType)(OldElementIndexEnd - 1) } };

		auto OldToNewIndex = [NewArrayIndex, OldElementIndexStart](FSlateInvalidationWidgetIndex OldIndex) -> FSlateInvalidationWidgetIndex
		{
			const IndexType NewElementIndex = OldIndex.ElementIndex - OldElementIndexStart;
			const FSlateInvalidationWidgetIndex NewWidgetIndex = { NewArrayIndex, NewElementIndex };
			return NewWidgetIndex;
		};

		// Copy and assign the new index to the widget
//Todo @pat fix this with proper iterator so that we do not have to use operator[]
		for (IndexType OldElementIndex = OldElementIndexStart; OldElementIndex < OldElementIndexEnd; ++OldElementIndex)
		{
			const FSlateInvalidationWidgetIndex MoveWidgetIndex = { OldArrayIndex, OldElementIndex };
			const IndexType NewElementIndex = (IndexType)Data[NewArrayIndex].ElementList.Add(MoveTemp((*this)[MoveWidgetIndex]));
			(*this)[MoveWidgetIndex].ResetWidget();
			const FSlateInvalidationWidgetIndex NewWidgetIndex = { NewArrayIndex, NewElementIndex };
			check(OldToNewIndex(MoveWidgetIndex) == NewWidgetIndex);
			InvalidationWidgetType& NewInvalidationWidget = (*this)[NewWidgetIndex];

			// Fix up Index
			NewInvalidationWidget.Index = NewWidgetIndex;

			// Fix up the parent index
			{
				if (OldRange.Include(FSlateInvalidationWidgetSortOrder{ *this, NewInvalidationWidget.ParentIndex }))
				{
					NewInvalidationWidget.ParentIndex = OldToNewIndex(NewInvalidationWidget.ParentIndex);
				}
			}

			// Fix up the leaf index
			{
				if (OldRange.Include(FSlateInvalidationWidgetSortOrder{ *this, NewInvalidationWidget.LeafMostChildIndex }))
				{
					NewInvalidationWidget.LeafMostChildIndex = OldToNewIndex(NewInvalidationWidget.LeafMostChildIndex);
				}
				check(NewInvalidationWidget.LeafMostChildIndex != FSlateInvalidationWidgetIndex::Invalid);
			}

			// Anyone in the hierarchy can point to a invalid LeafIndex.
			{
				InvalidationWidgetType* ParentInvalidationWidget = &(*this)[NewInvalidationWidget.ParentIndex];
				// If the Leaf is in the range, then recursive up.
				while (OldRange.Include(FSlateInvalidationWidgetSortOrder{ *this, ParentInvalidationWidget->LeafMostChildIndex }))
				{
					ParentInvalidationWidget->LeafMostChildIndex = OldToNewIndex(ParentInvalidationWidget->LeafMostChildIndex);
					if (ParentInvalidationWidget->ParentIndex == FSlateInvalidationWidgetIndex::Invalid)
					{
						break;
					}
					ParentInvalidationWidget = &(*this)[ParentInvalidationWidget->ParentIndex];
				}
			}

			// Set new index
			if (SWidget* Widget = NewInvalidationWidget.GetWidget())
			{
				FSlateInvalidationWidgetSortOrder SortIndex = { *this, NewWidgetIndex };
				Widget->SetFastPathProxyHandle(FWidgetProxyHandle{ Owner, NewWidgetIndex, SortIndex, GenerationNumber });
			}
		}

		// Random children may still point to the old ParentIndex
		check(Data[NewArrayIndex].ElementList.Num() == NewExpectedElementArraySize);
		check(Data[NewArrayIndex].StartIndex == 0);
		for (IndexType NewElementIndex = 0; NewElementIndex < NewExpectedElementArraySize; ++NewElementIndex)
		{
			const FSlateInvalidationWidgetIndex NewWidgetIndex = { NewArrayIndex, NewElementIndex };
			InvalidationWidgetType& NewInvalidationWidget = (*this)[NewWidgetIndex];

			// The parent is only invalid when it's the root. We can't remove it, but we shouldn't move it.
			check(NewInvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid);
			ForEachChildren(*this, NewInvalidationWidget, NewWidgetIndex, [NewWidgetIndex](InvalidationWidgetType& NewChildInvalidationWidget)
				{
					NewChildInvalidationWidget.ParentIndex = NewWidgetIndex;
				});
		}

		return OldElementIndexStart;
	}
	return INDEX_NONE;
}

#if WITH_SLATE_DEBUGGING
FSlateInvalidationWidgetIndex FSlateInvalidationWidgetList::FindWidget(const TSharedRef<SWidget> WidgetToFind) const
{
	SWidget* WidgetToFindPtr = &WidgetToFind.Get();
	for (FSlateInvalidationWidgetIndex Index = FirstIndex(); Index != FSlateInvalidationWidgetIndex::Invalid; Index = IncrementIndex(Index))
	{
		if (WidgetToFindPtr == (*this)[Index].GetWidget())
		{
			return Index;
		}
	}
	return FSlateInvalidationWidgetIndex::Invalid;
}


void FSlateInvalidationWidgetList::RemoveWidget(const FSlateInvalidationWidgetIndex WidgetIndex)
{
	if (WidgetIndex != FSlateInvalidationWidgetIndex::Invalid && IsValidIndex(WidgetIndex))
	{
		const InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];

		const FIndexRange Range = { *this, WidgetIndex, InvalidationWidget.LeafMostChildIndex };
		_RemoveRangeFromSameParent(Range);
	}
}


void FSlateInvalidationWidgetList::RemoveWidget(const TSharedRef<SWidget> WidgetToRemove)
{
	if (ensure(WidgetToRemove->GetProxyHandle().GetInvalidationRootHandle().GetUniqueId() == Owner.GetUniqueId()))
	{
		FSlateInvalidationWidgetIndex WidgetIndex = WidgetToRemove->GetProxyHandle().GetWidgetIndex();
		if (WidgetIndex != FSlateInvalidationWidgetIndex::Invalid)
		{
			const InvalidationWidgetType& InvalidationWidget = (*this)[WidgetIndex];
			const FIndexRange Range = { *this, WidgetIndex, InvalidationWidget.LeafMostChildIndex };
			_RemoveRangeFromSameParent(Range);
		}
	}

}


TArray<TSharedPtr<SWidget>> FSlateInvalidationWidgetList::FindChildren(const TSharedRef<SWidget> Widget) const
{
	TArray<TSharedPtr<SWidget>> Result;
	if (ensure(Widget->GetProxyHandle().GetInvalidationRootHandle().GetUniqueId() == Owner.GetUniqueId()))
	{
		FSlateInvalidationWidgetIndex WidgetIndex = Widget->GetProxyHandle().GetWidgetIndex();
		if (WidgetIndex == FSlateInvalidationWidgetIndex::Invalid)
		{
			return Result;
		}

		FMemMark Mark(FMemStack::Get());
		TArray<SWidget*, TMemStackAllocator<>> PreviousChildrenWidget;
		_FindChildren(WidgetIndex, PreviousChildrenWidget);

		Result.Reserve(PreviousChildrenWidget.Num());
		for (SWidget* WidgetPtr : PreviousChildrenWidget)
		{
			Result.Add(WidgetPtr ? WidgetPtr->AsShared() : TSharedPtr<SWidget>());
		}
	}
	return Result;
}


bool FSlateInvalidationWidgetList::DeapCompare(const FSlateInvalidationWidgetList& Other) const
{
	if (Root.Pin() != Other.Root.Pin())
	{
		return false;
	}

	FSlateInvalidationWidgetIndex IndexA = FirstIndex();
	FSlateInvalidationWidgetIndex IndexB = Other.FirstIndex();
	for (; IndexA != FSlateInvalidationWidgetIndex::Invalid && IndexB != FSlateInvalidationWidgetIndex::Invalid; IndexA = IncrementIndex(IndexA), IndexB = Other.IncrementIndex(IndexB))
	{
		const InvalidationWidgetType& InvalidationWidgetA = (*this)[IndexA];
		const InvalidationWidgetType& InvalidationWidgetB = Other[IndexB];
		if (InvalidationWidgetA.GetWidget() != InvalidationWidgetB.GetWidget())
		{
			return false;
		}
		if (InvalidationWidgetA.ParentIndex == FSlateInvalidationWidgetIndex::Invalid)
		{
			if (InvalidationWidgetA.ParentIndex != InvalidationWidgetB.ParentIndex)
			{
				return false;
			}
		}
		else
		{
			if ((*this)[InvalidationWidgetA.ParentIndex].GetWidget() != Other[InvalidationWidgetB.ParentIndex].GetWidget())
			{
				return false;
			}
		}
		check(InvalidationWidgetA.LeafMostChildIndex != FSlateInvalidationWidgetIndex::Invalid);
		check(InvalidationWidgetB.LeafMostChildIndex != FSlateInvalidationWidgetIndex::Invalid);
		if ((*this)[InvalidationWidgetA.LeafMostChildIndex].GetWidget() != Other[InvalidationWidgetB.LeafMostChildIndex].GetWidget())
		{
			return false;
		}
	}

	if (IndexA != FSlateInvalidationWidgetIndex::Invalid || IndexB != FSlateInvalidationWidgetIndex::Invalid)
	{
		return false;
	}

	return true;
}


void FSlateInvalidationWidgetList::LogWidgetsList()
{
	TStringBuilder<256> Builder;
	for (FSlateInvalidationWidgetIndex Index = FirstIndex(); Index != FSlateInvalidationWidgetIndex::Invalid; Index = IncrementIndex(Index))
	{
		Builder.Reset();
		const InvalidationWidgetType& InvalidateWidget = (*this)[Index];
		if (SWidget* Widget = InvalidateWidget.GetWidget())
		{
			Builder << Widget->GetTag();
		}
		else
		{
			Builder << TEXT("[None]");
		}
		Builder << TEXT("\t");
		if (InvalidateWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
		{
			if (SWidget* Widget = (*this)[InvalidateWidget.ParentIndex].GetWidget())
			{
				Builder << Widget->GetTag();
			}
			else
			{
				Builder << TEXT("[None]");
			}
		}
		else
		{
			Builder << TEXT("[---]");
		}
		Builder << TEXT("\t");
		if (InvalidateWidget.LeafMostChildIndex != FSlateInvalidationWidgetIndex::Invalid)
		{
			if (SWidget* Widget = (*this)[InvalidateWidget.LeafMostChildIndex].GetWidget())
			{
				Builder << Widget->GetTag();
			}
			else
			{
				Builder << TEXT("[None]");
			}
		}
		else
		{
			Builder << TEXT("[---]");
		}
		Builder << TEXT("\t");

		UE_LOG(LogSlate, Log, TEXT("%s"), Builder.ToString());
	}
}


bool FSlateInvalidationWidgetList::VerifyWidgetsIndex() const
{
	bool bResult = true;
	for (FSlateInvalidationWidgetIndex Index = FirstIndex(); Index != FSlateInvalidationWidgetIndex::Invalid; Index = IncrementIndex(Index))
	{
		const InvalidationWidgetType& InvalidateWidget = (*this)[Index];
		if (SWidget* Widget = InvalidateWidget.GetWidget())
		{
			const FSlateInvalidationWidgetIndex WidgetIndex = Widget->GetProxyHandle().GetWidgetIndex();
			if (Index != WidgetIndex)
			{
				UE_LOG(LogSlate, Warning, TEXT("Widget '%s' at index [%d,%d] is set to [%d,%d].")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
					, Index.ArrayIndex, Index.ElementIndex
					, WidgetIndex.ArrayIndex, WidgetIndex.ElementIndex);
				bResult = false;
			}
			else if (InvalidateWidget.Index != Index)
			{
				UE_LOG(LogSlate, Warning, TEXT("Widget '%s' at index [%d,%d] is set to the correct proxy index [%d,%d].")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
					, Index.ArrayIndex, Index.ElementIndex
					, WidgetIndex.ArrayIndex, WidgetIndex.ElementIndex);
				bResult = false;
			}
		}
		else
		{
			UE_LOG(LogSlate, Warning, TEXT("Widget at index [%d,%d] is [null]"), Index.ArrayIndex, Index.ElementIndex);
			bResult = false;
		}
	}
	return bResult;
}

bool FSlateInvalidationWidgetList::VerifyProxiesWidget() const
{
	bool bResult = true;
	for (const FArrayNode& Node : Data)
	{
		// Before StartIndex, pointer need to be empty.
		for (int32 ElementIndex = 0; ElementIndex < Node.StartIndex; ++ElementIndex)
		{
			if (SWidget* Widget = Node.ElementList[ElementIndex].GetWidget())
			{
				UE_LOG(LogSlate, Warning, TEXT("Element '%d' in the array of sort value '%d' as a valid widget '%s' when it should be set to none.")
					, ElementIndex, Node.SortOrder
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
				bResult = false;
			}
		}

		// Every other element need to point to a valid widget.
		for (int32 ElementIndex = Node.StartIndex; ElementIndex < Node.ElementList.Num(); ++ElementIndex)
		{
			SWidget* Widget = Node.ElementList[ElementIndex].GetWidget();
			if (!Widget)
			{
				UE_LOG(LogSlate, Warning, TEXT("Element '%d' in the array of sort value '%d' does not have a valid widget.")
					, ElementIndex, Node.SortOrder);
				bResult = false;
			}
		}
	}
	return bResult;
}

bool FSlateInvalidationWidgetList::VerifySortOrder() const
{
	bool bResult = true;
	if (FirstArrayIndex != INDEX_NONE)
	{
		int32 PreviousSortOrder = Data[FirstArrayIndex].SortOrder;
 		for (int32 ArrayIndex = Data[FirstArrayIndex].NextArrayIndex; ArrayIndex != INDEX_NONE; ArrayIndex = Data[ArrayIndex].NextArrayIndex)
		{
			if (PreviousSortOrder >= Data[ArrayIndex].SortOrder)
			{
				UE_LOG(LogSlate, Warning, TEXT("Array '%d' has a bigger sort order than previous array node '%d'.")
					, ArrayIndex, Data[ArrayIndex].PreviousArrayIndex);
				bResult = false;
				break;
			}
		}
	}
	return bResult;
}
#endif //WITH_SLATE_DEBUGGING

#undef UE_SLATE_VERIFY_INVALID_INVALIDATIONHANDLE
#undef UE_SLATE_VERIFY_REBUILDWIDGETDATA_ORDER
#undef UE_SLATE_VERIFY_REMOVED_WIDGET_ARE_NOT_INVALIDATED
