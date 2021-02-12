// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FastUpdate/SlateElementSortedArray.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "FastUpdate/SlateInvalidationWidgetIndex.h"
#include "FastUpdate/SlateInvalidationWidgetSortOrder.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"

#ifndef UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	#define UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

class FSlateInvalidationWidgetList
{
	friend struct FSlateInvalidationWidgetSortOrder;
public:
	using InvalidationWidgetType = FWidgetProxy;
private:
	using IndexType = FSlateInvalidationWidgetIndex::IndexType;
	using ElementListType = TArray<InvalidationWidgetType>;
	using WidgetListType = TSlateElementSortedArray<IndexType>;


public:
	/** */
	struct FIndexRange
	{
	private:
		FSlateInvalidationWidgetIndex InclusiveMin = FSlateInvalidationWidgetIndex::Invalid;
		FSlateInvalidationWidgetIndex InclusiveMax = FSlateInvalidationWidgetIndex::Invalid;
		FSlateInvalidationWidgetSortOrder OrderMin;
		FSlateInvalidationWidgetSortOrder OrderMax;

	public:
		FIndexRange() = default;
		FIndexRange(const FSlateInvalidationWidgetList& Self, FSlateInvalidationWidgetIndex InFrom, FSlateInvalidationWidgetIndex InEnd)
			: InclusiveMin(InFrom), InclusiveMax(InEnd)
			, OrderMin(Self, InFrom), OrderMax(Self, InEnd)
		{
			check(OrderMin <= OrderMax);
		}
		bool Include(FSlateInvalidationWidgetSortOrder Other) const
		{
			return OrderMin <= Other && Other <= OrderMax;
		}
		bool IsValid() const { return InclusiveMin != FSlateInvalidationWidgetIndex::Invalid; }

		FSlateInvalidationWidgetIndex GetInclusiveMinWidgetIndex() const { return InclusiveMin; }
		FSlateInvalidationWidgetIndex GetInclusiveMaxWidgetIndex() const { return InclusiveMax; }
		FSlateInvalidationWidgetSortOrder GetInclusiveMinWidgetSortOrder() const { return OrderMin; }
		FSlateInvalidationWidgetSortOrder GetInclusiveMaxWidgetSortOrder() const { return OrderMax; }

		bool operator==(const FIndexRange& Other) const { return Other.InclusiveMin == InclusiveMin && Other.InclusiveMax == InclusiveMax; }
	};

public:
	/** */
	struct FArguments
	{
		static int32 MaxPreferedElementsNum;
		static int32 MaxSortOrderPaddingBetweenArray;
		/**
		 * Prefered size of the elements array.
		 * The value should be between 2 and MaxPreferedElementsNum.
		 */
		int32 PreferedElementsNum = 64;

		/**
		 * When splitting, the elements will be copied to another array when the number of element is bellow this.
		 * The value should be between 1 and PreferedElementsNum.
		 */
		int32 NumberElementsLeftBeforeSplitting = 40;

		/**
		 * The sort order is used by the HittestGrid and the LayerId.
		 * The value should be between PreferedElementsNum and MaxSortOrderPaddingBetweenArray.
		 */
		int32 SortOrderPaddingBetweenArray = 1000;
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
		/** Change the Widget Index when building the array. Use when building temporary list. */
		bool bAssignedWidgetIndex = true;
#endif
	};
	FSlateInvalidationWidgetList(FSlateInvalidationRootHandle Owner, const FArguments& Args);

	/** Build the widget list from the root widget. */
	void BuildWidgetList(TSharedRef<SWidget> Root);

	/** Get the root the widget list was built with. */
	TWeakPtr<SWidget> GetRoot() { return Root; };
	/** Get the root the widget list was built with. */
	const TWeakPtr<SWidget> GetRoot() const { return Root; };

	/** Get the Generation number the list was built on. */
	int32 GetGenerationNumber() const { return GenerationNumber; }

	/** */
	struct IProcessChildOrderInvalidationCallback
	{
		struct FReIndexOperation
		{
			FReIndexOperation(FIndexRange const& InRange, FSlateInvalidationWidgetIndex InReIndexTarget) : Range(InRange), ReIndexTarget(InReIndexTarget) {}
			FIndexRange const& GetRange() const { return Range; }
			UE_NODISCARD FSlateInvalidationWidgetIndex ReIndex(FSlateInvalidationWidgetIndex Index) const;
		private:
			const FIndexRange& Range;
			FSlateInvalidationWidgetIndex ReIndexTarget = FSlateInvalidationWidgetIndex::Invalid;
		};
		struct FReSortOperation
		{
			FReSortOperation(FIndexRange const& InRange) : Range(InRange) {}
			FIndexRange const& GetRange() const { return Range; }
		private:
			FIndexRange const& Range;
		};

		/** Widget proxies that will be removed and will not be valid anymore. */
		virtual void PreChildRemove(FIndexRange const& Range) {}
		/** Widget proxies that got moved/re-indexed by the operation. */
		virtual void ProxiesReIndexed(FReIndexOperation const& Operation) {}
		/** Widget proxies that got resorted by the operation. */
		virtual void ProxiesPreResort(FReSortOperation const& Operation) {}
		/** Widget proxies that got resorted by the operation. */
		virtual void ProxiesPostResort() {}
		/** Widget proxies built by the operation. */
		virtual void ProxiesBuilt(FIndexRange const& Range) {}
	};

	/**
	 * Process widget that have a ChildOrder invalidation.
	 * @returns true if the InvalidationWidget is still valid.
	 */
	bool ProcessChildOrderInvalidation(InvalidationWidgetType const& InvalidationWidget, IProcessChildOrderInvalidationCallback& Callback);


	/**
	 * Test, then adds or removes from the registered attribute list.
	 * @returns true if the Widget go added to the registered attribute list.
	 */
	void ProcessAttributeRegistrationInvalidation(InvalidationWidgetType const& InvalidationWidget);

	/** Performs an operation on all SWidget in the list. */
	template<typename Predicate>
	void ForEachWidget(Predicate Pred)
	{
		int32 ArrayIndex = FirstArrayIndex;
		while (ArrayIndex != INDEX_NONE)
		{
			ElementListType& ElementList = Data[ArrayIndex].ElementList;
			const int32 ElementNum = ElementList.Num();
			for (int32 ElementIndex = Data[ArrayIndex].StartIndex; ElementIndex < ElementNum; ++ElementIndex)
			{
				SWidget* Widget = ElementList[ElementIndex].GetWidget();
				if (Widget)
				{
					Pred(Widget);
				}
			}

			ArrayIndex = Data[ArrayIndex].NextArrayIndex;
		}
	}

	/** Performs an operation on all InvalidationWidget in the list. */
	template<typename Predicate>
	void ForEachInvalidationWidget(Predicate Pred)
	{
		int32 ArrayIndex = FirstArrayIndex;
		while (ArrayIndex != INDEX_NONE)
		{
			ElementListType& ElementList = Data[ArrayIndex].ElementList;
			const int32 ElementNum = ElementList.Num();
			for (int32 ElementIndex = Data[ArrayIndex].StartIndex; ElementIndex < ElementNum; ++ElementIndex)
			{
				Pred(ElementList[ElementIndex]);
			}

			ArrayIndex = Data[ArrayIndex].NextArrayIndex;
		}
	}

	/** Iterator that goes over all the widgets with registered attribute. */
	struct FWidgetAttributeIterator
	{
	private:
		FSlateInvalidationWidgetList const& WidgetList;
		FSlateInvalidationWidgetIndex CurrentWidgetIndex;
		FSlateInvalidationWidgetSortOrder CurrentWidgetSortOrder;
		int32 AttributeIndex;

		FSlateInvalidationWidgetIndex MoveToWidgetIndexOnNextAdvance;

	public:
		FWidgetAttributeIterator(FSlateInvalidationWidgetList const& InWidgetList);

		//~ Handle operation
		void PreChildRemove(FIndexRange const& Range);
		void ReIndexed(IProcessChildOrderInvalidationCallback::FReIndexOperation const& Operation);
		void PostResort();
		void ProxiesBuilt(FIndexRange const& Range);
		void FixCurrentWidgetIndex();
		void Seek(FSlateInvalidationWidgetIndex SeekTo);

		/** Get the current widget index the iterator is pointing to. */
		FSlateInvalidationWidgetIndex GetCurrentIndex() const { return CurrentWidgetIndex; }
		
		/** Get the current widget sort order the iterator is pointing to. */
		FSlateInvalidationWidgetSortOrder GetCurrentSortOrder() const { return CurrentWidgetSortOrder; }

		/** Advance the iterator to the next valid widget index. */
		void Advance();

		/** Is the iterator pointing to a valid widget index. */
		bool IsValid() const { return CurrentWidgetIndex != FSlateInvalidationWidgetIndex::Invalid; }

	private:
		void AdvanceArrayIndex(int32 ArrayIndex);
	};

	FWidgetAttributeIterator CreateWidgetAttributeIterator()
	{
		return FWidgetAttributeIterator(*this);
	}

public:
	/** Returns reference to element at give index. */
	InvalidationWidgetType& operator[](const FSlateInvalidationWidgetIndex Index)
	{
		check(IsValidIndex(Index));
		return Data[Index.ArrayIndex].ElementList[Index.ElementIndex];
	}

	/** Returns reference to element at give index. */
	const InvalidationWidgetType& operator[](const FSlateInvalidationWidgetIndex Index) const
	{
		check(IsValidIndex(Index));
		return Data[Index.ArrayIndex].ElementList[Index.ElementIndex];
	}

	/** Tests if index is in the WidgetList range. */
	bool IsValidIndex(const FSlateInvalidationWidgetIndex Index) const
	{
		if (Data.IsValidIndex(Index.ArrayIndex))
		{
			return Index.ElementIndex >= Data[Index.ArrayIndex].StartIndex && Index.ElementIndex < Data[Index.ArrayIndex].ElementList.Num();
		}
		return false;
	}

	/** Returns true if there is not element in the WidgetList. */
	bool IsEmpty() const
	{
		return FirstArrayIndex == INDEX_NONE || Data[FirstArrayIndex].ElementList.Num() == 0;
	}

	/** Returns the first index from the WidgetList. */
	FSlateInvalidationWidgetIndex FirstIndex() const
	{
		return FirstArrayIndex == INDEX_NONE
			? FSlateInvalidationWidgetIndex::Invalid
			: FSlateInvalidationWidgetIndex{ (IndexType)FirstArrayIndex, Data[FirstArrayIndex].StartIndex };
	}

	/** Returns the last index from the WidgetList. */
	FSlateInvalidationWidgetIndex LastIndex() const
	{
		return LastArrayIndex == INDEX_NONE ? FSlateInvalidationWidgetIndex::Invalid : FSlateInvalidationWidgetIndex{ (IndexType)LastArrayIndex, (IndexType)(Data[LastArrayIndex].ElementList.Num() - 1) };
	}

	/** Increment a widget index to the next entry in the WidgetList. */
	UE_NODISCARD FSlateInvalidationWidgetIndex IncrementIndex(FSlateInvalidationWidgetIndex Index) const;

	/** Decrement a widget index to the next entry in the WidgetList. */
	UE_NODISCARD FSlateInvalidationWidgetIndex DecrementIndex(FSlateInvalidationWidgetIndex Index) const;

	/** Empties the WidgetList. */
	void Empty();

	/** Empties the WidgetList, but doesn't change the memory allocations. */
	void Reset();

public:
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	/** For testing purposes. Return the InvalidationWidgetIndex of the Widget within the InvalidationWidgetList. */
	FSlateInvalidationWidgetIndex FindWidget(const TSharedRef<SWidget> Widget) const;

	/** For testing purposes. Use ProcessChildOrderInvalidation. */
	void RemoveWidget(const FSlateInvalidationWidgetIndex ToRemove);
	/** For testing purposes. Use ProcessChildOrderInvalidation. */
	void RemoveWidget(const TSharedRef<SWidget> WidgetToRemove);

	/** For testing purposes. Use to test ProcessChildOrderInvalidation */
	UE_NODISCARD TArray<TSharedPtr<SWidget>> FindChildren(const TSharedRef<SWidget> Widget) const;

	/**
	 * For testing purposes.
	 * The list may not be the same, but the InvalidationWidgetType must:
	 * (1) be in the same order
	 * (2) point the same SWidget (itself, parent, leaf)
	 */
	bool DeapCompare(const FSlateInvalidationWidgetList& Other) const;

	/** For testing purposes. Log the tree. */
	void LogWidgetsList();

	/** For testing purposes. Verify that every widgets has the correct index. */
	bool VerifyWidgetsIndex() const;

	/** For testing purposes. Verify that every WidgetProxy has a valid SWidget. */
	bool VerifyProxiesWidget() const;

	/** For testing purposes. Verify that every the sorting order is increasing between widgets. */
	bool VerifySortOrder() const;

	/** For testing purposes. Verify that the ElementIndexList_ have valid indexes and are sorted. */
	bool VerifyElementIndexList() const;
#endif

private:
	bool ShouldDoRecursion(const SWidget* Widget) const
	{
		return !Widget->Advanced_IsInvalidationRoot() || IsEmpty();
	}
	bool ShouldDoRecursion(const TSharedRef<SWidget>& Widget) const
	{
		return !Widget->Advanced_IsInvalidationRoot() || IsEmpty();
	}
	bool ShouldBeAdded(const SWidget* Widget) const
	{
		return Widget != &(SNullWidget::NullWidget.Get());
	}
	bool ShouldBeAdded(const TSharedRef<SWidget>& Widget) const
	{
		return Widget != SNullWidget::NullWidget;
	}

private:
	template <typename... ArgsType>
	FSlateInvalidationWidgetIndex Emplace(ArgsType&&... Args)
	{
		const IndexType ArrayIndex = AddArrayNodeIfNeeded(true);
		const IndexType ElementIndex = (IndexType)Data[ArrayIndex].ElementList.Emplace(Forward<ArgsType>(Args)...);
		return FSlateInvalidationWidgetIndex{ ArrayIndex, ElementIndex };
	}
	template <typename... ArgsType>
	FSlateInvalidationWidgetIndex EmplaceInsertAfter(IndexType AfterArrayIndex, ArgsType&&... Args)
	{
		const IndexType ArrayIndex = InsertArrayNodeIfNeeded(AfterArrayIndex, true);
		const IndexType ElementIndex = (IndexType)Data[ArrayIndex].ElementList.Emplace(Forward<ArgsType>(Args)...);
		return FSlateInvalidationWidgetIndex{ ArrayIndex, ElementIndex };
	}

private:
	struct FCutResult
	{
		FCutResult() = default;

		/** Where in the previous array the reindexed element starts. */
		int32 OldElementIndexStart = INDEX_NONE;
	};
private:
	UE_NODISCARD IndexType AddArrayNodeIfNeeded(bool bReserveElementList);
	UE_NODISCARD IndexType InsertArrayNodeIfNeeded(IndexType AfterArrayIndex, bool bReserveElementList);
	UE_NODISCARD IndexType InsertDataNodeAfter(IndexType AfterIndex, bool bReserveElementList);
	void RemoveDataNode(IndexType Index);
	void RebuildOrderIndex(IndexType StartFrom);
	void UpdateParentLeafIndex(const InvalidationWidgetType& InvalidationWidget, FSlateInvalidationWidgetIndex OldIndex, FSlateInvalidationWidgetIndex NewIndex);
	FCutResult CutArray(const FSlateInvalidationWidgetIndex WhereToCut);

private:
	FSlateInvalidationWidgetIndex _BuildWidgetList_Recursive(TSharedRef<SWidget>& Widget, FSlateInvalidationWidgetIndex ParentIndex, IndexType& LastestIndex, bool bParentVisible, bool bParentVolatile);
	void _RebuildWidgetListTree(TSharedRef<SWidget> Widget, int32 ChildAtIndex);
	using FFindChildrenElement = TPair<SWidget*, FSlateInvalidationWidgetIndex>;
	void _FindChildren(FSlateInvalidationWidgetIndex WidgetIndex, TArray<FFindChildrenElement, TMemStackAllocator<>>& Widgets) const;
	void _RemoveRangeFromSameParent(const FIndexRange Range);
	FCutResult _CutArray(const FSlateInvalidationWidgetIndex WhereToCut);

private:
	struct FArrayNode
	{
		FArrayNode() = default;
		int32 PreviousArrayIndex = INDEX_NONE;
		int32 NextArrayIndex = INDEX_NONE;
		int32 SortOrder = 0;
		IndexType StartIndex = 0; // The array may start further in the ElementList as a result of a split.
		ElementListType ElementList;
		WidgetListType ElementIndexList_WidgetWithRegisteredSlateAttribute;

		void RemoveElementIndexBiggerOrEqualThan(IndexType ElementIndex);
	};
	using ArrayNodeType = TSparseArray<FArrayNode>;

	FSlateInvalidationRootHandle Owner;
	ArrayNodeType Data;
	TWeakPtr<SWidget> Root;
	int32 FirstArrayIndex = INDEX_NONE;
	int32 LastArrayIndex = INDEX_NONE;
	int32 GenerationNumber = INDEX_NONE;
	IProcessChildOrderInvalidationCallback* CurrentInvalidationCallback = nullptr;
	const FArguments WidgetListConfig;
};