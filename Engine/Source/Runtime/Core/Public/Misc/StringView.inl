// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Case-insensitive comparison operators
template<typename C>
inline bool operator==(const TStringViewImpl<C> Lhs, typename TIdentity<TStringViewImpl<C>>::Type Rhs)
{
	if (Rhs.Len() != Lhs.Len())
	{
		return false;
	}

	return FPlatformString::Strnicmp(Lhs.Data(), Rhs.Data(), Lhs.Len()) == 0;
}

template<typename C>
inline bool operator!=(const TStringViewImpl<C> Lhs, typename TIdentity<TStringViewImpl<C>>::Type Rhs)
{
	return !operator==(Lhs, Rhs);
}

template<typename C>
inline int32 TStringViewImpl<C>::Compare(const TStringViewImpl Other, ESearchCase::Type SearchCase) const
{
	const SizeType ShortestLength = FMath::Min(Len(), Other.Len());

	int Result;
	if (SearchCase == ESearchCase::CaseSensitive)
	{
		Result = FPlatformString::Strncmp(Data(), Other.Data(), ShortestLength);
	}
	else
	{
		Result = FPlatformString::Strnicmp(Data(), Other.Data(), ShortestLength);
	}

	if (Result == 0 && Len() != Other.Len())
	{
		return Len() > Other.Len() ? 1 : -1;
	}
	else
	{
		return Result;
	}
}

template<typename C>
inline bool TStringViewImpl<C>::EndsWith(const TStringViewImpl& Suffix) const
{
	const SizeType SuffixLen = Suffix.Len();
	if (SuffixLen > this->Len())
	{
		return false;
	}

	return 0 == TCString<C>::Strnicmp(Suffix.Data(), this->Data() + this->Len() - SuffixLen, SuffixLen);
}

template<typename C>
inline bool TStringViewImpl<C>::FindChar(C InChar, int32& OutIndex) const
{
	for (SizeType i = 0; i < Size; ++i)
	{
		if (DataPtr[i] == InChar)
		{
			OutIndex = (int32)i;
			return true;
		}
	}

	return false;
}

template<typename C>
inline bool TStringViewImpl<C>::FindLastChar(C InChar, int32& OutIndex) const
{
	if (Size == 0)
	{
		return false;
	}

	for (SizeType i = Size - 1; i >= 0; --i)
	{
		if (DataPtr[i] == InChar)
		{
			OutIndex = (int32)i;
			return true;
		}
	}

	return false;
}

template<typename C>
template<typename Predicate>
inline typename TStringViewImpl<C>::SizeType TStringViewImpl<C>::FindLastCharByPredicate(Predicate Pred, TStringViewImpl<C>::SizeType Count) const
{
	check(Count >= 0 && Count <= Len());

	for (const C* RESTRICT Start = Data(), *RESTRICT Data = Start + Count; Data != Start; )
	{
		--Data;
		if (Pred(*Data))
		{
			return static_cast<SizeType>(Data - Start);
		}
	}

	return INDEX_NONE;
}
