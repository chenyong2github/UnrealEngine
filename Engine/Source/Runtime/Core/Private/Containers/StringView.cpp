// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/StringView.h"

template class TStringViewImpl<TCHAR, FStringView>;
template class TStringViewImpl<ANSICHAR, FAnsiStringView>;
template class TStringViewImpl<WIDECHAR, FWideStringView>;

template <typename ViewType>
int32 StringViewPrivate::Compare(const ViewType& Lhs, const ViewType& Rhs, ESearchCase::Type SearchCase)
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

template int32 CORE_API StringViewPrivate::Compare(const FStringView& Lhs, const FStringView& Rhs, ESearchCase::Type SearchCase);
template int32 CORE_API StringViewPrivate::Compare(const FAnsiStringView& Lhs, const FAnsiStringView& Rhs, ESearchCase::Type SearchCase);
template int32 CORE_API StringViewPrivate::Compare(const FWideStringView& Lhs, const FWideStringView& Rhs, ESearchCase::Type SearchCase);

template <typename ViewType>
bool StringViewPrivate::FindChar(const ViewType& InView, typename ViewType::ElementType InChar, typename ViewType::SizeType& OutIndex)
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

template bool CORE_API StringViewPrivate::FindChar(const FStringView& InView, TCHAR InChar, typename FStringView::SizeType& OutIndex);
template bool CORE_API StringViewPrivate::FindChar(const FAnsiStringView& InView, ANSICHAR InChar, typename FAnsiStringView::SizeType& OutIndex);
template bool CORE_API StringViewPrivate::FindChar(const FWideStringView& InView, WIDECHAR InChar, typename FWideStringView::SizeType& OutIndex);

template <typename ViewType>
bool StringViewPrivate::FindLastChar(const ViewType& InView, typename ViewType::ElementType InChar, typename ViewType::SizeType& OutIndex)
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

template bool CORE_API StringViewPrivate::FindLastChar(const FStringView& InView, TCHAR InChar, typename FStringView::SizeType& OutIndex);
template bool CORE_API StringViewPrivate::FindLastChar(const FAnsiStringView& InView, ANSICHAR InChar, typename FAnsiStringView::SizeType& OutIndex);
template bool CORE_API StringViewPrivate::FindLastChar(const FWideStringView& InView, WIDECHAR InChar, typename FWideStringView::SizeType& OutIndex);
