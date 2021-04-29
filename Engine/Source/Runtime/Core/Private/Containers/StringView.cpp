// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/StringView.h"

template <typename CharType>
int32 TStringView<CharType>::Find(ViewType SubStr, SizeType StartPosition) const
{
	SizeType MinSearchLen = StartPosition + SubStr.Len();
	if (SubStr.IsEmpty() || Size < MinSearchLen)
	{
		return INDEX_NONE;
	}

	ElementType FirstSubChar = SubStr[0];
	SizeType RestSubLen = SubStr.Len() - 1;
	SizeType Maxi = Size - MinSearchLen;
	for (SizeType i = StartPosition; i <= Maxi; ++i)
	{
		if (DataPtr[i] == FirstSubChar)
		{
			if (RestSubLen == 0 || TCString<CharType>::Strncmp(DataPtr + i + 1, SubStr.DataPtr + 1, RestSubLen) == 0)
			{
				return i;
			}
		}
	}

	return INDEX_NONE;
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
