// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/StringView.h"

#include "Containers/UnrealString.h"
#include "HAL/PlatformString.h"

template<typename CharType, typename ViewType>
int32 TStringViewImpl<CharType, ViewType>::Compare(const TStringViewImpl& Other, ESearchCase::Type SearchCase) const
{
	const SizeType SelfLen = Len();
	const SizeType OtherLen = Other.Len();
	const SizeType ShortestLength = FMath::Min(SelfLen, OtherLen);

	int Result;
	if (SearchCase == ESearchCase::CaseSensitive)
	{
		Result = FPlatformString::Strncmp(GetData(), Other.GetData(), ShortestLength);
	}
	else
	{
		Result = FPlatformString::Strnicmp(GetData(), Other.GetData(), ShortestLength);
	}

	if (Result != 0 || SelfLen == OtherLen)
	{
		return Result;
	}

	return SelfLen < OtherLen ? -1 : 1;
}

template<typename CharType, typename ViewType>
bool TStringViewImpl<CharType, ViewType>::FindChar(CharType InChar, SizeType& OutIndex) const
{
	for (SizeType i = 0; i < Size; ++i)
	{
		if (DataPtr[i] == InChar)
		{
			OutIndex = i;
			return true;
		}
	}

	return false;
}

template<typename CharType, typename ViewType>
bool TStringViewImpl<CharType, ViewType>::FindLastChar(CharType InChar, SizeType& OutIndex) const
{
	if (Size == 0)
	{
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

	return false;
}

template class TStringViewImpl<TCHAR, FStringView>;
template class TStringViewImpl<ANSICHAR, FAnsiStringView>;
template class TStringViewImpl<WIDECHAR, FWideStringView>;
