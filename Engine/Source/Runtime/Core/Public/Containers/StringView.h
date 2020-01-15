// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Templates/AndOrNot.h"
#include "Templates/EnableIf.h"
#include "Templates/RemoveCV.h"
#include "Templates/UnrealTemplate.h"
#include "Traits/IsContiguousContainer.h"

class FAnsiStringView;
class FStringView;

namespace StringViewPrivate
{
	/**
	 * Trait testing whether a range type is compatible with the view type
	 */
	template <typename RangeType, typename ElementType>
	struct TIsCompatibleRangeType : TIsSame<ElementType, typename TRemoveCV<typename TRemovePointer<decltype(GetData(DeclVal<RangeType&>()))>::Type>::Type>
	{
	};

	template <typename CharType> struct TStringViewType;
	template <> struct TStringViewType<ANSICHAR> { using Type = FAnsiStringView; };
	template <> struct TStringViewType<TCHAR> { using Type = FStringView; };
}

/** The string view type for a given character type. */
template <typename CharType>
using TStringView = typename StringViewPrivate::TStringViewType<CharType>::Type;

/** String View

	* A string view is implicitly constructible from const char* style strings and
	  from character ranges such as FString.

	* A string view does not own any data nor does it attempt to control any lifetimes, it
	  merely points at a subrange of characters in some other string. It's up to the user
	  to ensure the underlying string stays valid for the lifetime of the string view.

	* A string view does not represent a NUL terminated string and therefore you should
	  never pass in the pointer returned by GetData() into a C-string API accepting only a
	  pointer. You must either use a string builder to make a properly terminated string,
	  or use an API that accepts a length argument in addition to the C-string.

	String views are a good fit for arguments to functions which don't wish to care
	which style of string construction is used by the caller. If you accept strings via
	string views then the caller is free to use FString, FStringBuilder, raw C strings,
	or any other type which can be converted into a string view.

	i.e., a function such as:

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

template <typename CharType>
class TStringViewImpl
{
public:
	using ElementType = CharType;
	using SizeType = int32;
	using ViewType = TStringView<ElementType>;

	template <typename RangeType>
	using TIsCompatibleRangeType = TAnd<TIsContiguousContainer<RangeType>, StringViewPrivate::TIsCompatibleRangeType<RangeType, ElementType>>;

	template <typename RangeType>
	using TEnableIfCompatibleRangeType = typename TEnableIf<TIsCompatibleRangeType<RangeType>::Value>::Type;

	/** Construct an empty view. */
	constexpr TStringViewImpl() = default;

	/**
	 * Construct a view of the null-terminated string pointed to by InData.
	 *
	 * The caller is responsible for ensuring that the provided character range remains valid for the lifetime of the view.
	 */
	inline TStringViewImpl(const CharType* InData)
		: DataPtr(InData)
		, Size(InData ? TCString<CharType>::Strlen(InData) : 0)
	{
	}

	/**
	 * Construct a view of InSize characters beginning at InData.
	 *
	 * The caller is responsible for ensuring that the provided character range remains valid for the lifetime of the view.
	 */
	constexpr inline TStringViewImpl(const CharType* InData, SizeType InSize)
		: DataPtr(InData)
		, Size(InSize)
	{
	}

	/** Access the character at the given index in the view. */
	inline const CharType& operator[](SizeType Index) const;

	/** Returns a pointer to the start of the view. This is NOT guaranteed to be null-terminated! */
	constexpr inline const CharType* GetData() const { return DataPtr; }

	/** Returns a pointer to the start of the view. This is NOT guaranteed to be null-terminated! */
	UE_DEPRECATED(4.25, "'Data' is deprecated. Please use 'GetData' instead!")
	constexpr inline const CharType* Data() const { return DataPtr; }

	// Capacity

	/** Returns the length of the string view. */
	constexpr inline SizeType Len() const { return Size; }

	/** Returns whether the string view is empty. */
	constexpr inline bool IsEmpty() const { return Size == 0; }

	// Modifiers

	/** Modifies the view to remove the given number of characters from the start. */
	inline void		RemovePrefix(SizeType CharCount)	{ DataPtr += CharCount; Size -= CharCount; }
	/** Modifies the view to remove the given number of characters from the end. */
	inline void		RemoveSuffix(SizeType CharCount)	{ Size -= CharCount; }

	// Operations

	/**
	 * Copy characters from the view into a destination buffer without null termination.
	 *
	 * @param Dest Buffer to write into. Must have space for at least CharCount characters.
	 * @param CharCount The maximum number of characters to copy.
	 * @param Position The offset into the view from which to start copying.
	 *
	 * @return The number of characters written to the destination buffer.
	 */
	inline SizeType CopyString(CharType* Dest, SizeType CharCount, SizeType Position = 0) const;

	/** Alias for Mid. */
	inline ViewType SubStr(SizeType Position, SizeType CharCount) const { return Mid(Position, CharCount); }

	/** Returns the left-most part of the view by taking the given number of characters from the left. */
	inline ViewType Left(SizeType CharCount) const;
	/** Returns the left-most part of the view by chopping the given number of characters from the right. */
	inline ViewType LeftChop(SizeType CharCount) const;
	/** Returns the right-most part of the view by taking the given number of characters from the right. */
	inline ViewType Right(SizeType CharCount) const;
	/** Returns the right-most part of the view by chopping the given number of characters from the left. */
	inline ViewType RightChop(SizeType CharCount) const;
	/** Returns the middle part of the view by taking up to the given number of characters from the given position. */
	inline ViewType Mid(SizeType Position, SizeType CharCount = TNumericLimits<SizeType>::Max()) const;
	/** Returns the middle part of the view between any whitespace at the start and end. */
	inline ViewType TrimStartAndEnd() const;
	/** Returns the right part of the view after any whitespace at the start. */
	CORE_API ViewType TrimStart() const;
	/** Returns the left part of the view before any whitespace at the end. */
	CORE_API ViewType TrimEnd() const;

	/** Modifies the view to be the given number of characters from the left. */
	inline void LeftInlineInline(SizeType CharCount) { *this = Left(CharCount); }
	/** Modifies the view by chopping the given number of characters from the right. */
	inline void LeftChopInline(SizeType CharCount) { *this = LeftChop(CharCount); }
	/** Modifies the view to be the given number of characters from the right. */
	inline void RightInline(SizeType CharCount) { *this = Right(CharCount); }
	/** Modifies the view by chopping the given number of characters from the left. */
	inline void RightChopInline(SizeType CharCount) { *this = RightChop(CharCount); }
	/** Modifies the view to be the middle part by taking up to the given number of characters from the given position. */
	inline void MidInline(SizeType Position, SizeType CharCount = TNumericLimits<SizeType>::Max()) { *this = Mid(Position, CharCount); }
	/** Modifies the view to be the middle part between any whitespace at the start and end. */
	inline void TrimStartAndEndInline() { *this = TrimStartAndEnd(); }
	/** Modifies the view to be the right part after any whitespace at the start. */
	inline void TrimStartInline() { *this = TrimStart(); }
	/** Modifies the view to be the left part before any whitespace at the end. */
	inline void TrimEndInline() { *this = TrimEnd(); }

	// Comparison

	/**
	 * Check whether this view is lexicographically equivalent to another view.
	 *
	 * @param SearchCase Whether the comparison should ignore case.
	 */
	inline bool Equals(const ViewType& Other, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive) const;

	/**
	 * Compare this view lexicographically with another view.
	 *
	 * @param SearchCase Whether the comparison should ignore case.
	 *
	 * @return 0 is equal, negative if this view is less, positive if this view is greater.
	 */
	CORE_API int32 Compare(const ViewType& Other, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive) const;

	/** Returns whether this view starts with the prefix character compared case-sensitively. */
	inline bool StartsWith(CharType Prefix) const { return Size >= 1 && DataPtr[0] == Prefix; }
	/** Returns whether this view starts with the prefix with optional case sensitivity. */
	inline bool StartsWith(const ViewType& Prefix, ESearchCase::Type SearchCase = ESearchCase::IgnoreCase) const;

	/** Returns whether this view ends with the suffix character compared case-sensitively. */
	inline bool EndsWith(CharType Suffix) const { return Size >= 1 && DataPtr[Size-1] == Suffix; }
	/** Returns whether this view ends with the suffix with optional case sensitivity. */
	inline bool EndsWith(const ViewType& Suffix, ESearchCase::Type SearchCase = ESearchCase::IgnoreCase) const;

	// Searching/Finding

	/**
	 * Search the view for the first occurrence of a character.
	 *
	 * @param InChar The character to search for. Comparison is lexicographic.
	 * @param OutIndex [out] The position at which the character was found, or INDEX_NONE if not found.
	 *
	 * @return Whether the character was found in the view.
	 */
	CORE_API bool FindChar(CharType InChar, SizeType& OutIndex) const;

	/**
	 * Search the view for the last occurrence of a character.
	 *
	 * @param InChar The character to search for. Comparison is lexicographic.
	 * @param OutIndex [out] The position at which the character was found, or INDEX_NONE if not found.
	 *
	 * @return Whether the character was found in the view.
	 */
	CORE_API bool FindLastChar(CharType InChar, SizeType& OutIndex) const;

public:
	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	constexpr inline const CharType* begin() const { return DataPtr; }
	constexpr inline const CharType* end() const { return DataPtr + Size; }

protected:
	const CharType* DataPtr = nullptr;
	SizeType Size = 0;
};

template <typename CharType>
struct TIsContiguousContainer<TStringViewImpl<CharType>>
{
	enum { Value = true };
};

template <typename CharType>
constexpr inline SIZE_T GetNum(const TStringViewImpl<CharType>& String)
{
	return String.Len();
}

//////////////////////////////////////////////////////////////////////////

class FStringView : public TStringViewImpl<TCHAR>
{
public:
	using TStringViewImpl<ElementType>::TStringViewImpl;
};

template <> struct TIsContiguousContainer<FStringView> { enum { Value = true }; };

constexpr inline FStringView operator "" _SV(const TCHAR* String, size_t Size) { return FStringView(String, Size); }

//////////////////////////////////////////////////////////////////////////

class FAnsiStringView : public TStringViewImpl<ANSICHAR>
{
public:
	using TStringViewImpl<ElementType>::TStringViewImpl;
};

template <> struct TIsContiguousContainer<FAnsiStringView> { enum { Value = true }; };

constexpr inline FAnsiStringView operator "" _ASV(const ANSICHAR* String, size_t Size) { return FAnsiStringView(String, Size); }

//////////////////////////////////////////////////////////////////////////

using FWideStringView = FStringView;

constexpr inline FWideStringView operator "" _WSV(const WIDECHAR* String, size_t Size) { return FWideStringView(String, Size); }

//////////////////////////////////////////////////////////////////////////

#include "StringView.inl"
