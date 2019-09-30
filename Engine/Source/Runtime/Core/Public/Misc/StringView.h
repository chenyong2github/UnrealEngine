// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/CString.h"
#include "Containers/UnrealString.h"

/** String View

	* A string view is implicitly constructible from FString and const char* style strings
	* A string view does not own any data nor does it attempt to control any lifetimes, it 
	  merely points at a subrange of characters in some other string. It's up to the user
	  to ensure the underlying string stays valid for the lifetime of the string view.

  */

template<typename C>
class TStringViewImpl
{
public:
	inline TStringViewImpl(const C* InData)
	:	DataPtr(InData)
	,	Size(TCString<C>::Strlen(InData))
	{
	}

	inline TStringViewImpl(const C* InData, SIZE_T InSize)
	:	DataPtr(InData)
	,	Size(InSize)
	{
	}

	inline const C& operator[](SIZE_T Pos) const	{ return DataPtr[Pos]; }

	// Q: should this even be here? Semantics differ from FString as it won't return 
	//    a null terminated string
	inline const C* operator*() const				{ return DataPtr; }			

	inline const C* Data() const					{ return DataPtr; }

	// Capacity

	inline SIZE_T	Len() const						{ return Size; }
	inline bool		IsEmpty() const					{ return Size == 0; }

	// Modifiers

	inline void		RemovePrefix(SIZE_T CharCount)	{ DataPtr += CharCount; Size -= CharCount; }
	inline void		RemoveSuffix(SIZE_T CharCount)	{ Size -= CharCount; }

	// Operations

	inline SIZE_T			CopyString(C* Dest, SIZE_T CharCount, SIZE_T Position = 0)	{ memcpy(Dest, DataPtr + Position, FMath::Min(Size - Position, CharCount)); }
	inline TStringViewImpl& SubStr(SIZE_T Position, SIZE_T CharCount)					{ return TStringViewImpl(DataPtr + Position, FMath::Min(Size - Position, CharCount)); }

	// Comparison

	// Compare

	inline bool				StartsWith(const TStringViewImpl& Prefix) const				{ return 0 == TCString<C>::Strnicmp(Prefix.Data(), this->Data(), Prefix.Len()); }
	inline bool				StartsWith(C Prefix) const									{ return Size >= 1 && DataPtr[0] == Prefix; }

	// EndsWith

	inline bool				EndsWith(const TStringViewImpl& Suffix) const 
	{ 
		const SIZE_T SuffixLen = Suffix.Len();
		if (SuffixLen > this->Len())
		{
			return false;
		}

		return 0 == TCString<C>::Strnicmp(Suffix.Data(), this->Data() + this->Len() - SuffixLen, SuffixLen);
	}

	// Find

	int32 Find(const FString& SubStr, ESearchCase::Type SearchCase = ESearchCase::IgnoreCase, ESearchDir::Type SearchDir = ESearchDir::FromStart) const
	{
		// :TODO:
		return 0;
	}

	inline bool FindChar(C InChar, int32& OutIndex) const
	{
		for (SIZE_T i = 0; i < Size; ++i)
		{
			if (DataPtr[i] == InChar)
			{
				OutIndex = (int32) i;
				return true;
			}
		}

		return false;
	}

	inline bool FindLastChar(C InChar, int32& OutIndex) const
	{
		if (Size == 0)
		{
			return false;
		}

		for (SIZE_T i = Size - 1; i >= 0; --i)
		{
			if (DataPtr[i] == InChar)
			{
				OutIndex = (int32)i;
				return true;
			}
		}

		return false;
	}

	// FindLastOf
	// FindFirstNotOf
	// FindLastNotOf

	inline const C* begin() const					{ return DataPtr; }
	inline const C* end() const						{ return DataPtr + Size; }

protected:
	const C*	DataPtr;
	SIZE_T		Size;
};

template<typename C>
inline bool operator==(const TStringViewImpl<C>& lhs, const TStringViewImpl<C>& rhs)
{
	if (rhs.Len() != lhs.Len())
	{
		return false;
	}

	return lhs.StartsWith(rhs);
}

template<typename C>
inline bool operator!=(const TStringViewImpl<C>& lhs, const TStringViewImpl<C>& rhs)
{
	return !operator==(lhs, rhs);
}

//////////////////////////////////////////////////////////////////////////

class FAnsiStringView : public TStringViewImpl<ANSICHAR>
{
public:
	FAnsiStringView(const ANSICHAR* InString, SIZE_T InStrlen)
	:	TStringViewImpl<ANSICHAR>(InString, InStrlen)
	{
	}

	FAnsiStringView(const ANSICHAR* InString)
	:	TStringViewImpl<ANSICHAR>(InString)
	{
	}
};

class FWideStringView : public TStringViewImpl<WIDECHAR>
{
public:
	FWideStringView(const FString& InString)
	:	TStringViewImpl<WIDECHAR>(*InString, InString.Len())
	{
	}

	FWideStringView(const WIDECHAR* InString)
	:	TStringViewImpl<WIDECHAR>(InString)
	{
	}

	FWideStringView(const WIDECHAR* InString, SIZE_T InStrlen)
	:	TStringViewImpl<WIDECHAR>(InString, InStrlen)
	{
	}
};

using FStringView = FWideStringView;
