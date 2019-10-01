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

	* A string view does not represent a NUL terminated string and therefore you should
	  never pass in the pointer returned by Data() into a C-string API accepting only a
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

template<typename C>
class TStringViewImpl
{
public:
	typedef int32 SizeType;

	inline TStringViewImpl(const C* InData)
	:	DataPtr(InData)
	,	Size(TCString<C>::Strlen(InData))
	{
	}

	inline TStringViewImpl(const C* InData, SizeType InSize)
	:	DataPtr(InData)
	,	Size(InSize)
	{
	}

	inline const C& operator[](SizeType Pos) const	{ return DataPtr[Pos]; }

	inline const C* Data() const					{ return DataPtr; }

	// Capacity

	inline SizeType	Len() const						{ return Size; }
	inline bool		IsEmpty() const					{ return Size == 0; }

	// Modifiers

	inline void		RemovePrefix(SizeType CharCount)	{ DataPtr += CharCount; Size -= CharCount; }
	inline void		RemoveSuffix(SizeType CharCount)	{ Size -= CharCount; }

	// Operations

	inline SIZE_T			CopyString(C* Dest, SizeType CharCount, SizeType Position = 0)	{ memcpy(Dest, DataPtr + Position, FMath::Min(Size - Position, CharCount)); }
	inline TStringViewImpl& SubStr(SizeType Position, SizeType CharCount)					{ return TStringViewImpl(DataPtr + Position, FMath::Min(Size - Position, CharCount)); }

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

	inline bool FindChar(C InChar, int32& OutIndex) const
	{
		for (SizeType i = 0; i < Size; ++i)
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

	// FindLastOf
	// FindFirstNotOf
	// FindLastNotOf

	inline const C* begin() const					{ return DataPtr; }
	inline const C* end() const						{ return DataPtr + Size; }

protected:
	const C*	DataPtr;
	SizeType	Size;
};

// Case-insensitive comparison operators

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
	FAnsiStringView(const ANSICHAR* InString, SizeType InStrlen)
	:	TStringViewImpl<ANSICHAR>(InString, InStrlen)
	{
	}

	FAnsiStringView(const ANSICHAR* InString)
	:	TStringViewImpl<ANSICHAR>(InString)
	{
	}
};

class FStringView : public TStringViewImpl<TCHAR>
{
public:
	FStringView(const FString& InString)
	:	TStringViewImpl<TCHAR>(*InString, InString.Len())
	{
	}

	FStringView(const TCHAR* InString)
	:	TStringViewImpl<TCHAR>(InString)
	{
	}

	FStringView(const TCHAR* InString, SizeType InStrlen)
	:	TStringViewImpl<TCHAR>(InString, InStrlen)
	{
	}

	/** Convert to a dynamic string

		Remember that a string view is not necessarily
		NUL terminated so whenever you end up passing a
		string into an API which accepts a plain C string
		then you will need to ensure it's NUL terminated
		first. This is one way of doing that, involving
		a memory allocation as well as a string copy,
		along with a memory deallocation when the string
		goes away.

		I would encourage you to use a TStringBuilder<>
		instead since this avoids hitting the heap, and
		the heap is always more expensive even if it
		seems seductively convenient.
	  */
	CORE_API FString ToString() const;
};
