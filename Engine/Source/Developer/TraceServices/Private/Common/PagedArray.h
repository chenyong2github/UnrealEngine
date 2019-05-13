// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/SlabAllocator.h"

struct FNoPageData
{
};

template<typename ItemType, typename PageDataType = FNoPageData>
class TPagedArray
{
public:

	struct FPageInternal;

	class FIterator
	{
	public:
		const PageDataType* GetCurrentPage()
		{
			if (CurrentPage)
			{
				uint64 CurrentPageIndex = CurrentPage - FirstPage;
				return Outer.PageUserDatas.GetData() + CurrentPageIndex;
			}
			else
			{
				return nullptr;
			}
		}

		const ItemType* GetCurrentItem()
		{
			return CurrentItem;
		}

		const PageDataType* PrevPage()
		{
			if (CurrentPage == FirstPage)
			{
				return nullptr;
			}
			--CurrentPage;
			CurrentPageBegin = CurrentPage->Items - 1;
			CurrentPageEnd = CurrentPage->Items + CurrentPage->Count;
			CurrentItem = CurrentPageEnd - 1;
			return GetCurrentPage();
		}

		const PageDataType* NextPage()
		{
			if (CurrentPage == LastPage)
			{
				return nullptr;
			}
			++CurrentPage;
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

		FIterator(const TPagedArray& InOuter, uint64 InitialBankIndex, uint64 InitialItemIndex)
			: Outer(InOuter)
		{
			if (Outer.Pages.Num() > 0)
			{
				FirstPage = Outer.Pages.GetData();
				LastPage = Outer.Pages.GetData() + Outer.Pages.Num() - 1;
				check(InitialBankIndex < Outer.Pages.Num());
				CurrentPage = Outer.Pages.GetData() + InitialBankIndex;
				CurrentPageBegin = CurrentPage->Items - 1;
				CurrentPageEnd = CurrentPage->Items + CurrentPage->Count;
				check(InitialItemIndex < CurrentPage->Count);
				CurrentItem = CurrentPage->Items + InitialItemIndex;
			}
		}

		const TPagedArray& Outer;
		const FPageInternal* CurrentPage = nullptr;
		const FPageInternal* FirstPage = nullptr;
		const FPageInternal* LastPage = nullptr;
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
		for (FPageInternal& Page : Pages)
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

	ItemType& PushBack()
	{
		if (!LastPage || LastPage->Count == PageSize)
		{
			LastPage = &Pages.AddDefaulted_GetRef();
			LastPage->Items = Allocator.Allocate<ItemType>(PageSize);
			PageUserDatas.AddDefaulted_GetRef();
		}
		++TotalItemCount;
		ItemType* ItemPtr = LastPage->Items + LastPage->Count;
		new (ItemPtr) ItemType();
		++LastPage->Count;
		return *ItemPtr;
	}

	ItemType& Insert(uint64 Index)
	{
		PushBack();
		for (uint64 CurrentIndex = TotalItemCount - 1; CurrentIndex > Index; --CurrentIndex)
		{
			(*this)[CurrentIndex] = (*this)[CurrentIndex - 1];
		}
		return (*this)[Index];
	}

	PageDataType* GetLastPage()
	{
		if (PageUserDatas.Num())
		{
			return &PageUserDatas.Last();
		}
		else
		{
			return nullptr;
		}
	}

	PageDataType* GetPage(uint64 PageIndex)
	{
		return PageUserDatas.GetData() + PageIndex;
	}

	PageDataType* GetItemPage(uint64 ItemIndex)
	{
		uint64 PageIndex = ItemIndex / PageSize;
		return PageUserDatas.GetData() + PageIndex;
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

	const TArray<PageDataType> GetPages() const
	{
		return PageUserDatas;
	}

	ItemType& operator[](uint64 Index)
	{
		uint64 PageIndex = Index / PageSize;
		uint64 IndexInPage = Index % PageSize;
		FPageInternal* Page = Pages.GetData() + PageIndex;
		ItemType* Item = Page->Items + IndexInPage;
		return *Item;
	}

	const ItemType& operator[](uint64 Index) const
	{
		return const_cast<TPagedArray&>(*this)[Index];
	}

private:
	struct FPageInternal
	{
		ItemType* Items = nullptr;
		uint64 Count = 0;
	};

	FSlabAllocator& Allocator;
	TArray<FPageInternal> Pages;
	TArray<PageDataType> PageUserDatas;
	FPageInternal* LastPage = nullptr;
	uint64 PageSize;
	uint64 TotalItemCount = 0;
};
