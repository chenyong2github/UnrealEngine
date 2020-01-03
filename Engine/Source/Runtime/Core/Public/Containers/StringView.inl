// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/UnrealMemory.h"
#include "Templates/Decay.h"
#include "Templates/UnrealTypeTraits.h"
#include "Traits/IntType.h"

template <typename CharType>
inline const CharType& TStringViewImpl<CharType>::operator[](SizeType Index) const
{
	checkf(Index >= 0 && Index < Size, TEXT("Index out of bounds on StringView: index %i on a view with a length of %i"), Index, Size);
	return DataPtr[Index];
}

template <typename CharType>
inline typename TStringViewImpl<CharType>::SizeType TStringViewImpl<CharType>::CopyString(CharType* Dest, SizeType CharCount, SizeType Position) const
{
	const  SizeType CopyCount = FMath::Min(Size - Position, CharCount);
	FMemory::Memcpy(Dest, DataPtr + Position, CopyCount);
	return CopyCount;
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::Left(SizeType CharCount) const
{
	return ViewType(DataPtr, FMath::Clamp(CharCount, 0, Size));
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::LeftChop(SizeType CharCount) const
{
	return ViewType(DataPtr, FMath::Clamp(Size - CharCount, 0, Size));
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::Right(SizeType CharCount) const
{
	const SizeType OutLen = FMath::Clamp(CharCount, 0, Size);
	return ViewType(DataPtr + Size - OutLen, OutLen);
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::RightChop(SizeType CharCount) const
{
	const SizeType OutLen = FMath::Clamp(Size - CharCount, 0, Size);
	return ViewType(DataPtr + Size - OutLen, OutLen);
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::Mid(SizeType Position, SizeType CharCount) const
{
	check(CharCount >= 0);
	using USizeType = TUnsignedIntType_T<sizeof(SizeType)>;
	Position = FMath::Clamp<USizeType>(Position, 0, Size);
	CharCount = FMath::Clamp<USizeType>(CharCount, 0, Size - Position);
	return ViewType(DataPtr + Position, CharCount);
}

template <typename CharType>
inline TStringView<CharType> TStringViewImpl<CharType>::TrimStartAndEnd() const
{
	return TrimStart().TrimEnd();
}

template <typename CharType>
inline bool TStringViewImpl<CharType>::Equals(const ViewType& Other, ESearchCase::Type SearchCase) const
{
	return Size == Other.Size && Compare(Other, SearchCase) == 0;
}

template <typename CharType>
inline bool TStringViewImpl<CharType>::StartsWith(const ViewType& Prefix, ESearchCase::Type SearchCase) const
{
	return Prefix.Equals(Left(Prefix.Len()), SearchCase);
}

template <typename CharType>
inline bool TStringViewImpl<CharType>::EndsWith(const ViewType& Suffix, ESearchCase::Type SearchCase) const
{
	return Suffix.Equals(Right(Suffix.Len()), SearchCase);
}

// Case-insensitive comparison operators

template <typename CharType, typename RangeType,
	typename = typename TStringViewImpl<CharType>::template TEnableIfCompatibleRangeType<RangeType>>
inline bool operator==(const TStringViewImpl<CharType>& Lhs, RangeType&& Rhs)
{
	return Lhs.Equals(TStringView<CharType>(Forward<RangeType>(Rhs)), ESearchCase::IgnoreCase);
}

template <typename CharType, typename RangeType,
	typename = typename TEnableIf<TNot<TIsDerivedFrom<typename TDecay<RangeType>::Type, TStringViewImpl<CharType>>>::Value>::Type>
inline auto operator==(RangeType&& Lhs, const TStringViewImpl<CharType>& Rhs) -> decltype(Rhs == Forward<RangeType>(Lhs))
{
	return Rhs == Forward<RangeType>(Lhs);
}

template <typename CharType, typename RangeType>
inline auto operator!=(const TStringViewImpl<CharType>& Lhs, RangeType&& Rhs) -> decltype(!(Lhs == Forward<RangeType>(Rhs)))
{
	return !(Lhs == Forward<RangeType>(Rhs));
}

template <typename CharType, typename RangeType,
	typename = typename TEnableIf<TNot<TIsDerivedFrom<typename TDecay<RangeType>::Type, TStringViewImpl<CharType>>>::Value>::Type>
inline auto operator!=(RangeType&& Lhs, const TStringViewImpl<CharType>& Rhs) -> decltype(!(Rhs == Forward<RangeType>(Lhs)))
{
	return !(Rhs == Forward<RangeType>(Lhs));
}

// Case-insensitive C-string comparison operators

template <typename CharType>
inline bool operator==(const TStringViewImpl<CharType>& Lhs, const CharType* Rhs)
{
	return TCString<CharType>::Strnicmp(Lhs.GetData(), Rhs, Lhs.Len()) == 0 && !Rhs[Lhs.Len()];
}

template <typename CharType>
inline bool operator==(const CharType* Lhs, const TStringViewImpl<CharType>& Rhs)
{
	return Rhs == Lhs;
}

template <typename CharType>
inline bool operator!=(const TStringViewImpl<CharType>& Lhs, const CharType* Rhs)
{
	return !(Lhs == Rhs);
}

template <typename CharType>
inline bool operator!=(const CharType* Lhs, const TStringViewImpl<CharType>& Rhs)
{
	return !(Lhs == Rhs);
}
