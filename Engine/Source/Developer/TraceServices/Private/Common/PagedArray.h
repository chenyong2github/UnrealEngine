// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/SlabAllocator.h"

template<typename ItemType>
struct TPagedArrayPage
{
	ItemType* Items = nullptr;
	uint64 Count = 0;
};

template<typename ItemType>
inline const ItemType* GetData(const TPagedArrayPage<ItemType>& Page)
{
	return Page.Items;
}

template<typename ItemType>
inline SIZE_T GetNum(const TPagedArrayPage<ItemType>& Page)
{
	return Page.Count;
}

template<typename ItemType>
inline const ItemType* GetFirstItem(const TPagedArrayPage<ItemType>& Page)
{
	return Page.Items;
}

template<typename ItemType>
inline const ItemType* GetLastItem(const TPagedArrayPage<ItemType>& Page)
{
	if (Page.Count)
	{
		return Page.Items + Page.Count - 1;
	}
	else
	{
		return nullptr;
	}
}

template<typename InItemType, typename InPageType = TPagedArrayPage<InItemType>>
class TPagedArray
{
public:
	typedef InItemType ItemType;
	typedef InPageType PageType;
	
	class FIterator
	{
	public:
		uint64 GetCurrentPageIndex()
		{
			return CurrentPageIndex;
		}

		const PageType* GetCurrentPage()
		{
			return CurrentPage;
		}

		const ItemType* GetCurrentItem()
		{
			return CurrentItem;
		}

		const PageType* PrevPage()
		{
			if (CurrentPageIndex == 0)
			{
				return nullptr;
			}
			--CurrentPageIndex;
			CurrentPage = Outer.FirstPage + CurrentPageIndex;
			CurrentPageBegin = CurrentPage->Items - 1;
			CurrentPageEnd = CurrentPage->Items + CurrentPage->Count;
			CurrentItem = CurrentPageEnd - 1;
			return GetCurrentPage();
		}

		const PageType* NextPage()
		{
			if (CurrentPageIndex == Outer.PagesArray.Num() - 1)
			{
				return nullptr;
			}
			++CurrentPageIndex;
			CurrentPage = Outer.FirstPage + CurrentPageIndex;
			CurrentPageBegin = CurrentPage->Items - 1;
			CurrentPageEnd = CurrentPage->Items + CurrentPage->Count;
			CurrentItem = CurrentPage->Items;
			return GetCurrentPage();
		}

		const ItemType* NextItem()
		{
			++CurrentItem;
			if (CurrentItem >= CurrentPageEnd && !NextPage())
			{
				return nullptr;
			}
			return CurrentItem;
		}

		const ItemType* PrevItem()
		{
			--CurrentItem;
			if (CurrentItem <= CurrentPageBegin && !PrevPage())
			{
				return nullptr;
			}
			return CurrentItem;
		}

	private:
		friend class TPagedArray;

		FIterator(const TPagedArray& InOuter, uint64 InitialPageIndex, uint64 InitialItemIndex)
			: Outer(InOuter)
		{
			if (Outer.PagesArray.Num())
			{
				check(InitialPageIndex < Outer.PagesArray.Num());
				CurrentPageIndex = InitialPageIndex;
				CurrentPage = Outer.FirstPage + InitialPageIndex;
				CurrentPageBegin = CurrentPage->Items - 1;
				CurrentPageEnd = CurrentPage->Items + CurrentPage->Count;
				check(InitialItemIndex < CurrentPage->Count);
				CurrentItem = CurrentPage->Items + InitialItemIndex;
			}
		}

		const TPagedArray& Outer;
		const PageType* CurrentPage = nullptr;
		uint64 CurrentPageIndex = 0;
		const ItemType* CurrentItem = nullptr;
		const ItemType* CurrentPageBegin = nullptr;
		const ItemType* CurrentPageEnd = nullptr;
	};

	TPagedArray(FSlabAllocator& InAllocator, uint64 InPageSize)
		: Allocator(InAllocator)
		, PageSize(InPageSize)
	{

	}

	~TPagedArray()
	{
		for (PageType& Page : PagesArray)
		{
			ItemType* PageEnd = Page.Items + Page.Count;
			for (ItemType* Item = Page.Items; Item != PageEnd; ++Item)
			{
				Item->~ItemType();
			}
		}
	}

	uint64 Num() const
	{
		return TotalItemCount;
	}

	uint64 GetPageSize() const
	{
		return PageSize;
	}

	uint64 NumPages() const
	{
		return PagesArray.Num();
	}

	ItemType& PushBack()
	{
		if (!LastPage || LastPage->Count == PageSize)
		{
			LastPage = &PagesArray.AddDefaulted_GetRef();
			FirstPage = PagesArray.GetData();
			LastPage->Items = Allocator.Allocate<ItemType>(PageSize);
		}
		++TotalItemCount;
		ItemType* ItemPtr = LastPage->Items + LastPage->Count;
		new (ItemPtr) ItemType();
		++LastPage->Count;
		return *ItemPtr;
	}

	ItemType& Insert(uint64 Index)
	{
		if (Index >= TotalItemCount)
		{
			return PushBack();
		}
		PushBack();
		uint64 PageIndex = Index / PageSize;
		uint64 PageItemIndex = Index % PageSize;
		for (uint64 CurrentPageIndex = PagesArray.Num() - 1; CurrentPageIndex > PageIndex; --CurrentPageIndex)
		{
			PageType* CurrentPage = FirstPage + CurrentPageIndex;
			memmove(CurrentPage->Items + 1, CurrentPage->Items, sizeof(ItemType) * (CurrentPage->Count - 1));
			PageType* PrevPage = CurrentPage - 1;
			memcpy(CurrentPage->Items, PrevPage->Items + PrevPage->Count - 1, sizeof(ItemType));
		}
		PageType* Page = FirstPage + PageIndex;
		memmove(Page->Items + PageItemIndex + 1, Page->Items + PageItemIndex, sizeof(ItemType) * (Page->Count - PageItemIndex - 1));
		return Page->Items[PageItemIndex];
	}

	PageType* GetLastPage()
	{
		return LastPage;
	}

	PageType* GetPage(uint64 PageIndex)
	{
		return FirstPage + PageIndex;
	}

	PageType* GetItemPage(uint64 ItemIndex)
	{
		uint64 PageIndex = ItemIndex / PageSize;
		return FirstPage + PageIndex;
	}

	FIterator GetIteratorFromPage(uint64 PageIndex) const
	{
		return FIterator(*this, PageIndex, 0);
	}

	FIterator GetIteratorFromItem(uint64 ItemIndex) const
	{
		uint64 PageIndex = ItemIndex / PageSize;
		uint64 IndexInPage = ItemIndex % PageSize;
		return FIterator(*this, PageIndex, IndexInPage);
	}

	const PageType* GetPages() const
	{
		return FirstPage;
	}

	ItemType& operator[](uint64 Index)
	{
		uint64 PageIndex = Index / PageSize;
		uint64 IndexInPage = Index % PageSize;
		PageType* Page = FirstPage + PageIndex;
		ItemType* Item = Page->Items + IndexInPage;
		return *Item;
	}

	const ItemType& operator[](uint64 Index) const
	{
		return const_cast<TPagedArray&>(*this)[Index];
	}

private:
	FSlabAllocator& Allocator;
	TArray<PageType> PagesArray;
	PageType* FirstPage = nullptr;
	PageType* LastPage = nullptr;
	uint64 PageSize;
	uint64 TotalItemCount = 0;
};

template<typename ItemType, typename PageType>
inline const PageType* GetData(const TPagedArray<ItemType, PageType>& PagedArray)
{
	return PagedArray.GetPages();
}

template<typename ItemType, typename PageType>
inline SIZE_T GetNum(const TPagedArray<ItemType, PageType>& PagedArray)
{
	return PagedArray.NumPages();
}