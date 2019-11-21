// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HAL/PlatformString.h"
#include "HAL/UnrealMemory.h"

template <typename CharType, typename ViewType>
inline typename TStringViewImpl<CharType, ViewType>::SizeType TStringViewImpl<CharType, ViewType>::CopyString(CharType* Dest, SizeType CharCount, SizeType Position) const
{
	SizeType CopyCount = FMath::Min(Size - Position, CharCount);
	FMemory::Memcpy(Dest, DataPtr + Position, CopyCount);
	return CopyCount;
}

// Case-insensitive comparison operators

template <typename CharType, typename ViewType>
inline bool operator==(const TStringViewImpl<CharType, ViewType>& Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return Lhs.Len() == Rhs.Len() && FPlatformString::Strnicmp(Lhs.GetData(), Rhs.GetData(), Lhs.Len()) == 0;
}

template <typename CharType, typename ViewType>
inline bool operator!=(const TStringViewImpl<CharType, ViewType>& Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return !operator==(Lhs, Rhs);
}

// Case-insensitive range comparison operators

template <typename CharType, typename ViewType, typename RangeType, typename = typename TStringViewImpl<CharType, ViewType>::template TEnableIfCompatibleRangeType<RangeType>>
inline bool operator==(const TStringViewImpl<CharType, ViewType>& Lhs, const RangeType& Rhs)
{
	return Lhs == TStringViewImpl<CharType, ViewType>(Rhs);
}

template <typename CharType, typename ViewType, typename RangeType, typename = typename TStringViewImpl<CharType, ViewType>::template TEnableIfCompatibleRangeType<RangeType>>
inline bool operator==(const RangeType& Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return TStringViewImpl<CharType, ViewType>(Lhs) == Rhs;
}

template <typename CharType, typename ViewType, typename RangeType, typename = typename TStringViewImpl<CharType, ViewType>::template TEnableIfCompatibleRangeType<RangeType>>
inline bool operator!=(const TStringViewImpl<CharType, ViewType>& Lhs, const RangeType& Rhs)
{
	return !operator==(Lhs, Rhs);
}

template <typename CharType, typename ViewType, typename RangeType, typename = typename TStringViewImpl<CharType, ViewType>::template TEnableIfCompatibleRangeType<RangeType>>
inline bool operator!=(const RangeType& Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return !operator==(Lhs, Rhs);
}

// Case-insensitive C-string comparison operators

template <typename CharType, typename ViewType>
inline bool operator==(const TStringViewImpl<CharType, ViewType>& Lhs, const CharType* Rhs)
{
	return TCString<CharType>::Strnicmp(Lhs.GetData(), Rhs, Lhs.Len()) == 0 && !Rhs[Lhs.Len()];
}

template <typename CharType, typename ViewType>
inline bool operator==(const CharType* Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return TCString<CharType>::Strnicmp(Lhs, Rhs.GetData(), Rhs.Len()) == 0 && !Lhs[Rhs.Len()];
}

template <typename CharType, typename ViewType>
inline bool operator!=(const TStringViewImpl<CharType, ViewType>& Lhs, const CharType* Rhs)
{
	return !operator==(Lhs, Rhs);
}

template <typename CharType, typename ViewType>
inline bool operator!=(const CharType* Lhs, const TStringViewImpl<CharType, ViewType>& Rhs)
{
	return !operator==(Lhs, Rhs);
}
