// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/StringView.h"

template <typename CharType>
int32 TStringView<CharType>::Compare(const ViewType Rhs, const ESearchCase::Type SearchCase) const
{
	const SizeType LhsLen = Len();
	const SizeType RhsLen = Rhs.Len();
	const SizeType MinLen = FMath::Min(LhsLen, RhsLen);

	int Result;
	if (SearchCase == ESearchCase::CaseSensitive)
	{
		Result = TCString<ElementType>::Strncmp(GetData(), Rhs.GetData(), MinLen);
	}
	else
	{
		Result = TCString<ElementType>::Strnicmp(GetData(), Rhs.GetData(), MinLen);
	}

	if (Result != 0 || LhsLen == RhsLen)
	{
		return Result;
	}

	return LhsLen < RhsLen ? -1 : 1;
}

template <typename CharType>
bool TStringView<CharType>::FindChar(const ElementType InChar, SizeType& OutIndex) const
{
	for (SizeType i = 0; i < Size; ++i)
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


template <typename CharType>
bool TStringView<CharType>::FindLastChar(const ElementType InChar, SizeType& OutIndex) const
{
	if (Size == 0)
	{
		OutIndex = INDEX_NONE;
		return false;
	}

	for (SizeType i = Size - 1; i >= 0; --i)
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

template <typename CharType>
TStringView<CharType> TStringView<CharType>::TrimStart() const
{
	SizeType SpaceCount = 0;
	for (ElementType Char : *this)
	{
		if (!TChar<ElementType>::IsWhitespace(Char))
		{
			break;
		}
		++SpaceCount;
	}
	return ViewType(DataPtr + SpaceCount, Size - SpaceCount);
}

template <typename CharType>
TStringView<CharType> TStringView<CharType>::TrimEnd() const
{
	SizeType NewSize = Size;
	while (NewSize && TChar<ElementType>::IsWhitespace(DataPtr[NewSize - 1]))
	{
		--NewSize;
	}
	return ViewType(DataPtr, NewSize);
}

template class TStringView<ANSICHAR>;
template class TStringView<WIDECHAR>;
