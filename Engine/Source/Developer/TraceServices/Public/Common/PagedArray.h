// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Containers/Allocators.h"
#include "Containers/Array.h"

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

template<typename ItemType, typename PageType>
class TPagedArray;

template<typename ItemType, typename PageType>
class TPagedArrayIterator
{
public:
	TPagedArrayIterator()
	{

	}

	TPagedArrayIterator(const TPagedArray<ItemType, PageType>& InOuter, uint64 InitialPageIndex, uint64 InitialItemIndex)
		: Outer(&InOuter)
	{
		if (Outer->PagesArray.Num())
		{
			check(InitialPageIndex < Outer->PagesArray.Num());
			CurrentPageIndex = InitialPageIndex;
			OnCurrentPageChanged();
			PageType* CurrentPage = Outer->FirstPage + CurrentPageIndex;
			check(InitialItemIndex < CurrentPage->Count);
			CurrentItem = CurrentPage->Items + InitialItemIndex;
		}
	}

	uint64 GetCurrentPageIndex()
	{
		return CurrentPageIndex;
	}

	const PageType* GetCurrentPage()
	{
		return Outer->FirstPage + CurrentPageIndex;
	}

	const ItemType* GetCurrentItem()
	{
		return CurrentItem;
	}

	const ItemType& operator*() const
	{
		return *CurrentItem;
	}

	const ItemType* operator->() const
	{
		return CurrentItem;
	}

	explicit operator bool() const
	{
		return CurrentItem != nullptr;
	}

	const PageType* PrevPage()
	{
		if (CurrentPageIndex == 0)
		{
			CurrentItem = nullptr;
			CurrentPageFirstItem = nullptr;
			CurrentPageLastItem = nullptr;
			return nullptr;
		}
		--CurrentPageIndex;
		OnCurrentPageChanged();
		CurrentItem = CurrentPageLastItem;
		return GetCurrentPage();
	}

	const PageType* NextPage()
	{
		if (CurrentPageIndex == Outer->PagesArray.Num() - 1)
		{
			CurrentItem = nullptr;
			CurrentPageFirstItem = nullptr;
			CurrentPageLastItem = nullptr;
			return nullptr;
		}
		++CurrentPageIndex;
		OnCurrentPageChanged();
		CurrentItem = CurrentPageFirstItem;
		return GetCurrentPage();
	}

	const ItemType* NextItem()
	{
		if (CurrentItem == CurrentPageLastItem)
		{
			if (!NextPage())
			{
				return nullptr;
			}
			else
			{
				return CurrentItem;
			}
		}
		++CurrentItem;
		return CurrentItem;
	}

	TPagedArrayIterator& operator++()
	{
		NextItem();
		return *this;
	}

	TPagedArrayIterator operator++(int)
	{
		TPagedArrayIterator Tmp(*this);
		NextItem();
		return Tmp;
	}

	const ItemType* PrevItem()
	{
		if (CurrentItem == CurrentPageFirstItem)
		{
			if (!PrevPage())
			{
				return nullptr;
			}
			else
			{
				return CurrentItem;
			}
		}
		--CurrentItem;
		return CurrentItem;
	}

	TPagedArrayIterator& operator--()
	{
		PrevItem();
		return *this;
	}

	TPagedArrayIterator operator--(int)
	{
		TPagedArrayIterator Tmp(*this);
		PrevItem();
		return Tmp;
	}

	const ItemType* SetPosition(uint64 Index)
	{
		uint64 PageIndex = Index / Outer->PageSize;
		uint64 ItemIndexInPage = Index % Outer->PageSize;
		if (PageIndex != CurrentPageIndex)
		{
			check(PageIndex < Outer->PagesArray.Num());
			CurrentPageIndex = PageIndex;
			OnCurrentPageChanged();
		}
		PageType* CurrentPage = Outer->FirstPage + CurrentPageIndex;
		check(ItemIndexInPage < CurrentPage->Count);
		CurrentItem = CurrentPage->Items + ItemIndexInPage;
		return CurrentItem;
	}

private:
	void OnCurrentPageChanged()
	{
		PageType* CurrentPage = Outer->FirstPage + CurrentPageIndex;
		CurrentPageFirstItem = CurrentPage->Items;
		if (CurrentPage->Items)
		{
			CurrentPageLastItem = CurrentPage->Items + CurrentPage->Count - 1;
		}
		else
		{
			CurrentPageLastItem = nullptr;
		}
	}

	const TPagedArray<ItemType, PageType>* Outer = nullptr;
	const ItemType* CurrentItem = nullptr;
	const ItemType* CurrentPageFirstItem = nullptr;
	const ItemType* CurrentPageLastItem = nullptr;
	uint64 CurrentPageIndex = 0;
};

template<typename InItemType, typename InPageType = TPagedArrayPage<InItemType>>
class TPagedArray
{
public:
	typedef InItemType ItemType;
	typedef InPageType PageType;
	typedef TPagedArrayIterator<InItemType, InPageType> TIterator;

	TPagedArray(Trace::ILinearAllocator& InAllocator, uint64 InPageSize)
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
			LastPage->Items = reinterpret_cast<ItemType*>(Allocator.Allocate(PageSize * sizeof(ItemType)));
		}
		++TotalItemCount;
		ItemType* ItemPtr = LastPage->Items + LastPage->Count;
		new (ItemPtr) ItemType();
		++LastPage->Count;
		return *ItemPtr;
	}

	template <typename... ArgsType>
	ItemType& EmplaceBack(ArgsType&&... Args)
	{
		if (!LastPage || LastPage->Count == PageSize)
		{
			LastPage = &PagesArray.AddDefaulted_GetRef();
			FirstPage = PagesArray.GetData();
			LastPage->Items = reinterpret_cast<ItemType*>(Allocator.Allocate(PageSize * sizeof(ItemType)));
		}
		++TotalItemCount;
		ItemType* ItemPtr = LastPage->Items + LastPage->Count;
		new (ItemPtr) ItemType(Forward<ArgsType>(Args)...);
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

	TIterator GetIterator() const
	{
		return TIterator(*this, 0, 0);
	}

	TIterator GetIteratorFromPage(uint64 PageIndex) const
	{
		return TIterator(*this, PageIndex, 0);
	}

	TIterator GetIteratorFromItem(uint64 ItemIndex) const
	{
		uint64 PageIndex = ItemIndex / PageSize;
		uint64 IndexInPage = ItemIndex % PageSize;
		return TIterator(*this, PageIndex, IndexInPage);
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

	ItemType& First()
	{
		ItemType* Item = FirstPage->Items;
		return *Item;
	}

	const ItemType& First() const
	{
		const ItemType* Item = FirstPage->Items;
		return *Item;
	}

	ItemType& Last()
	{
		ItemType* Item = LastPage->Items + LastPage->Count - 1;
		return *Item;
	}

	const ItemType& Last() const
	{
		const ItemType* Item = LastPage->Items + LastPage->Count - 1;
		return *Item;
	}

private:
	template<typename ItemType, typename PageType>
	friend class TPagedArrayIterator;

	Trace::ILinearAllocator& Allocator;
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