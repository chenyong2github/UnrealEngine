// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Containers/StringView.h"

template class TStringViewImpl<ANSICHAR, FAnsiStringView>;
template class TStringViewImpl<WIDECHAR, FWideStringView>;

namespace StringViewPrivate
{
	template <typename ViewType>
	inline int32 CompareImpl(const ViewType& Lhs, const ViewType& Rhs, ESearchCase::Type SearchCase)
	{
		const typename ViewType::SizeType LhsLen = Lhs.Len();
		const typename ViewType::SizeType RhsLen = Rhs.Len();
		const typename ViewType::SizeType MinLen = FMath::Min(LhsLen, RhsLen);

		int Result;
		if (SearchCase == ESearchCase::CaseSensitive)
		{
			Result = TCString<typename ViewType::ElementType>::Strncmp(Lhs.GetData(), Rhs.GetData(), MinLen);
		}
		else
		{
			Result = TCString<typename ViewType::ElementType>::Strnicmp(Lhs.GetData(), Rhs.GetData(), MinLen);
		}

		if (Result != 0 || LhsLen == RhsLen)
		{
			return Result;
		}

		return LhsLen < RhsLen ? -1 : 1;
	}

	int32 Compare(const FAnsiStringView& Lhs, const FAnsiStringView& Rhs, ESearchCase::Type SearchCase)
	{
		return CompareImpl(Lhs, Rhs, SearchCase);
	}

	int32 Compare(const FWideStringView& Lhs, const FWideStringView& Rhs, ESearchCase::Type SearchCase)
	{
		return CompareImpl(Lhs, Rhs, SearchCase);
	}

	template <typename ViewType>
	inline bool FindCharImpl(const ViewType& InView, typename ViewType::ElementType InChar, typename ViewType::SizeType& OutIndex)
	{
		const typename ViewType::ElementType* DataPtr = InView.GetData();
		const typename ViewType::SizeType Size = InView.Len();

		for (typename ViewType::SizeType i = 0; i < Size; ++i)
		{
			if (DataPtr[i] == InChar)
			{
				OutIndex = i;
				return true;
			}
		}

		OutIndex = INDEX_NONE;
		return false;
	}

	bool FindChar(const FAnsiStringView& InView, ANSICHAR InChar, typename FAnsiStringView::SizeType& OutIndex)
	{
		return FindCharImpl(InView, InChar, OutIndex);
	}

	bool FindChar(const FWideStringView& InView, WIDECHAR InChar, typename FWideStringView::SizeType& OutIndex)
	{
		return FindCharImpl(InView, InChar, OutIndex);
	}

	template <typename ViewType>
	bool FindLastCharImpl(const ViewType& InView, typename ViewType::ElementType InChar, typename ViewType::SizeType& OutIndex)
	{
		const typename ViewType::ElementType* DataPtr = InView.GetData();
		const typename ViewType::SizeType Size = InView.Len();

		if (Size == 0)
		{
			OutIndex = INDEX_NONE;
			return false;
		}

		for (typename ViewType::SizeType i = Size - 1; i >= 0; --i)
		{
			if (DataPtr[i] == InChar)
			{
				OutIndex = i;
				return true;
			}
		}

		OutIndex = INDEX_NONE;
		return false;
	}

	bool FindLastChar(const FAnsiStringView& InView, ANSICHAR InChar, typename FAnsiStringView::SizeType& OutIndex)
	{
		return FindLastCharImpl(InView, InChar, OutIndex);
	}

	bool FindLastChar(const FWideStringView& InView, WIDECHAR InChar, typename FWideStringView::SizeType& OutIndex)
	{
		return FindLastCharImpl(InView, InChar, OutIndex);
	}

	template <typename ViewType>
	inline ViewType TrimStartImpl(ViewType InView)
	{
		typename ViewType::SizeType SpaceCount = 0;
		for (typename ViewType::ElementType Char : InView)
		{
			if (!TChar<typename ViewType::ElementType>::IsWhitespace(Char))
			{
				break;
			}
			++SpaceCount;
		}
		InView.RemovePrefix(SpaceCount);
		return InView;
	}

	FAnsiStringView TrimStart(const FAnsiStringView& InView)
	{
		return TrimStartImpl(InView);
	}

	FWideStringView TrimStart(const FWideStringView& InView)
	{
		return TrimStartImpl(InView);
	}

	template <typename ViewType>
	inline ViewType TrimEndImpl(const ViewType& InView)
	{
		const typename ViewType::ElementType* DataPtr = InView.GetData();
		typename ViewType::SizeType Len = InView.Len();
		while (Len && TChar<typename ViewType::ElementType>::IsWhitespace(DataPtr[Len - 1]))
		{
			--Len;
		}
		return ViewType(DataPtr, Len);
	}

	FAnsiStringView TrimEnd(const FAnsiStringView& InView)
	{
		return TrimEndImpl(InView);
	}

	FWideStringView TrimEnd(const FWideStringView& InView)
	{
		return TrimEndImpl(InView);
	}
}
