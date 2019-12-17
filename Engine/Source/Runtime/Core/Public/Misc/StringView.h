// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Templates/AndOrNot.h"
#include "Templates/EnableIf.h"
#include "Templates/RemoveCV.h"
#include "Templates/UnrealTemplate.h"
#include "Traits/IsContiguousContainer.h"

namespace StringViewPrivate
{
	/**
	 * Trait testing whether a range type is compatible with the view type
	 */
	template <typename RangeType, typename ElementType>
	struct TIsCompatibleRangeType : TIsSame<ElementType, typename TRemoveCV<typename TRemovePointer<decltype(GetData(DeclVal<RangeType&>()))>::Type>::Type>
	{
	};
}

/** String View

	* A string view is implicitly constructible from const char* style strings and
	  from character ranges such as FString.

	* A string view does not own any data nor does it attempt to control any lifetimes, it 
	  merely points at a subrange of characters in some other string. It's up to the user
	  to ensure the underlying string stays valid for the lifetime of the string view.

	* A string view does not represent a NUL terminated string and therefore you should
	  never pass in the pointer returned by GetData() into a C-string API accepting only a
	  pointer. You must either use a string builder to make a properly terminated string,
	  or the ToString() function below if you absolutely must, and can live with the
	  knowledge that you just added yet one more memory allocation even though memory
	  allocations are not cheap.

	String views are a good fit for arguments to functions which don't wish to care
	which style of string construction is used by the caller. If you accept strings via
	string views then the caller is free to use FString, FStringBuilder or raw C strings
	or any other type which can be converted into a string builder.

	I.e a function such as

	void DoFoo(const FStringView& InString);

	May be called as:

	void MultiFoo()
	{
	     FString MyFoo(TEXT("Zoo"));
		 const TCHAR* MyFooStr = *MyFoo;

		 TStringBuilder<64> BuiltFoo;
		 BuiltFoo.Append(TEXT("ABC"));

		 DoFoo(MyFoo);
		 DoFoo(MyFooStr);
		 DoFoo(TEXT("ABC"));
		 DoFoo(BuiltFoo);
	}

  */

template <typename CharType, typename DerivedViewType>
class TStringViewImpl
{
public:
	using ElementType = CharType;
	using SizeType = int32;
	using ViewType = DerivedViewType;

	template <typename RangeType>
	using TIsCompatibleRangeType = TAnd<TIsContiguousContainer<RangeType>, StringViewPrivate::TIsCompatibleRangeType<RangeType, ElementType>>;

	template <typename RangeType>
	using TEnableIfCompatibleRangeType = typename TEnableIf<TIsCompatibleRangeType<RangeType>::Value>::Type;

	TStringViewImpl() = default;

	TStringViewImpl(const CharType* InData)
		: DataPtr(InData)
		, Size(TCString<CharType>::Strlen(InData))
	{
	}

	TStringViewImpl(const CharType* InData, SizeType InSize)
		: DataPtr(InData)
		, Size(InSize)
	{
	}

	inline const CharType& operator[](SizeType Pos) const { return DataPtr[Pos]; }

	inline const CharType* GetData() const { return DataPtr; }

	UE_DEPRECATED(4.25, "'Data' is deprecated. Please use 'GetData' instead!")
	inline const CharType* Data() const { return DataPtr; }

	// Capacity

	inline SizeType Len() const { return Size; }
	inline bool IsEmpty() const { return Size == 0; }

	// Modifiers

	inline void		RemovePrefix(SizeType CharCount)	{ DataPtr += CharCount; Size -= CharCount; }
	inline void		RemoveSuffix(SizeType CharCount)	{ Size -= CharCount; }

	// Operations

	inline SizeType CopyString(CharType* Dest, SizeType CharCount, SizeType Position = 0) const;
	inline ViewType SubStr(SizeType Position, SizeType CharCount) const						{ check(Position <= Len()); return ViewType(DataPtr + Position, FMath::Min(Size - Position, CharCount)); }

	// Maintain compatibility with FString 
	inline ViewType Left(SizeType CharCount) const											{ return ViewType(GetData(), FMath::Clamp(CharCount, 0, Len())); }
	inline ViewType LeftChop(SizeType CharCount) const										{ return ViewType(GetData(), FMath::Clamp(Len() - CharCount, 0, Len())); }
	inline ViewType Right(SizeType CharCount) const											{ return ViewType(GetData() + Len() - FMath::Clamp(CharCount, 0, Len())); }
	inline ViewType RightChop(SizeType CharCount) const										{ return ViewType(GetData() + Len() - FMath::Clamp(Len() - CharCount, 0, Len())); }
	inline ViewType Mid(SizeType Position, SizeType CharCount) const						{ return SubStr(Position, CharCount); /*This is just a wrapper around SubStr to keep compatibility with the FString interface*/} 

	// Comparison
	inline bool				Equals(const TStringViewImpl& Other, ESearchCase::Type SearchCase) const { return Len() == Other.Len() && Compare(Other, SearchCase) == 0; }
	CORE_API int32			Compare(const TStringViewImpl& Other, ESearchCase::Type SearchCase) const;

	inline bool StartsWith(CharType Prefix) const										{ return Size >= 1 && DataPtr[0] == Prefix; }
	inline bool StartsWith(const TStringViewImpl& Prefix) const							{ return Len() >= Prefix.Len() && TCString<CharType>::Strnicmp(GetData(), Prefix.GetData(), Prefix.Len()) == 0; }

	inline bool EndsWith(CharType Suffix) const											{ return Size >= 1 && DataPtr[Size-1] == Suffix; }
	inline bool EndsWith(const TStringViewImpl& Suffix) const							{ return Len() >= Suffix.Len() && TCString<CharType>::Strnicmp(GetData() + Len() - Suffix.Len(), Suffix.GetData(), Suffix.Len()) == 0; }

	// Searching/Finding
	CORE_API bool FindChar(CharType InChar, SizeType& OutIndex) const;
	CORE_API bool FindLastChar(CharType InChar, SizeType& OutIndex) const;

public:
	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	inline const CharType* begin() const { return DataPtr; }
	inline const CharType* end() const { return DataPtr + Size; }

protected:
	const CharType* DataPtr = nullptr;
	SizeType Size = 0;
};

template <typename CharType, typename ViewType>
struct TIsContiguousContainer<TStringViewImpl<CharType, ViewType>>
{
	enum { Value = true };
};

template <typename CharType, typename ViewType>
inline SIZE_T GetNum(const TStringViewImpl<CharType, ViewType>& String)
{
	return String.Len();
}

//////////////////////////////////////////////////////////////////////////

class FStringView : public TStringViewImpl<TCHAR, FStringView>
{
public:
	using TStringViewImpl<ElementType, FStringView>::TStringViewImpl;
};

class FAnsiStringView : public TStringViewImpl<ANSICHAR, FAnsiStringView>
{
public:
	using TStringViewImpl<ElementType, FAnsiStringView>::TStringViewImpl;
};

class FWideStringView : public TStringViewImpl<WIDECHAR, FWideStringView>
{
public:
	using TStringViewImpl<ElementType, FWideStringView>::TStringViewImpl;
};

#include "StringView.inl"
