// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/CString.h"
#include "Misc/StringView.h"

#define USE_STRING_LITERAL_PATH 1

/** String Builder implementation

	This class helps with the common task of constructing new strings. 
	
	It does this by allocating buffer space which is used to hold the 
	constructed string. The intent is that the builder is allocated on
	the stack as a function local variable to avoid heap allocations.

	The buffer is always contiguous and the class is not intended to be
	used to construct extremely large strings.

	The amount of buffer space to allocate is specified via a template
	parameter and if the constructed string should overflow this initial
	buffer, a new buffer will be allocated using regular dynamic memory
	allocations. For instances where you absolutely must not allocate
	any memory, you should use the fixed variants named
	TFixed...StringBuilder

	Overflow allocation should be the exceptional case however -- always
	try to size the buffer so that it can hold the vast majority strings
	you expect to construct.

	Be mindful that stack is a limited resource, so if you are writing a
	highly recursive function you may want to use some other mechanism
	to build your strings.

  */

template<typename C>
class TStringBuilderImpl
{
public:
	TStringBuilderImpl() = default;
	CORE_API ~TStringBuilderImpl();

	TStringBuilderImpl(const TStringBuilderImpl&) = delete;
	TStringBuilderImpl(TStringBuilderImpl&&) = delete;
	const TStringBuilderImpl& operator=(const TStringBuilderImpl&) = delete;
	const TStringBuilderImpl& operator=(TStringBuilderImpl&&) = delete;

	inline SIZE_T	Len() const			{ return CurPos - Base; }
	inline const C* ToString() const	{ EnsureNulTerminated(); return Base; }
	inline const C* operator*() const	{ EnsureNulTerminated(); return Base; }

	inline const C	LastChar() const			{ return *(CurPos - 1); }

	inline TStringBuilderImpl& Append(C Char)
	{
		EnsureCapacity(1);

		*CurPos++ = Char;

		return *this;
	}

#if USE_STRING_LITERAL_PATH
	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR*& NulTerminatedString)
#else
	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR* NulTerminatedString)
#endif
	{
		const SIZE_T Length = TCString<ANSICHAR>::Strlen(NulTerminatedString);

		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (SIZE_T i = 0; i < Length; ++i)
		{
			Dest[i] = NulTerminatedString[i];
		}

		return *this;
	}

	inline TStringBuilderImpl& AppendAnsi(const FAnsiStringView& AnsiString)
	{
		return AppendAnsi(AnsiString.Data(), AnsiString.Len());
	}

	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR* NulTerminatedString, const SIZE_T Length)
	{
		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (SIZE_T i = 0; i < Length; ++i)
		{
			Dest[i] = NulTerminatedString[i];
		}

		return *this;
	}

#if USE_STRING_LITERAL_PATH
	inline TStringBuilderImpl& Append(const C*& NulTerminatedString)
#else
	inline TStringBuilderImpl& Append(const C* NulTerminatedString)
#endif
	{
		const SIZE_T Length = TCString<C>::Strlen(NulTerminatedString);

		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, NulTerminatedString, Length * sizeof(C));

		return *this;
	}

	inline TStringBuilderImpl& Append(const C* NulTerminatedString, SIZE_T MaxChars)
	{
		const SIZE_T StringLength = TCString<C>::Strlen(NulTerminatedString);
		const SIZE_T Length = FPlatformMath::Min<SIZE_T>(MaxChars, StringLength);

		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, NulTerminatedString, Length * sizeof(C));

		return *this;
	}

#if USE_STRING_LITERAL_PATH
	template<SIZE_T ArrayLength>
	inline TStringBuilderImpl& Append(C(&CharArray)[ArrayLength])
	{
		return Append(&CharArray[0]);
	}

	template<SIZE_T LiteralLength>
	inline TStringBuilderImpl& Append(const C (&StringLiteral)[LiteralLength])
	{
		constexpr SIZE_T Length = LiteralLength - 1;
		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, StringLiteral, Length * sizeof(C));

		return *this;
	}

	template<SIZE_T LiteralLength>
	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR(&StringLiteral)[LiteralLength])
	{
		constexpr SIZE_T Length = LiteralLength - 1;
		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (SIZE_T i = 0; i < Length; ++i)
		{
			Dest[i] = StringLiteral[i];
		}

		return *this;
	}
#endif

protected:
	inline void Initialize(C* InBase, SIZE_T InCapacity)
	{
		Base	= InBase;
		CurPos	= InBase;
		End		= Base + InCapacity;
	}

	inline void EnsureNulTerminated() const
	{
		if (*CurPos)
		{
			*CurPos = 0;
		}
	}

	inline void EnsureCapacity(SIZE_T RequiredAdditionalCapacity)
	{
		// precondition: we know the current buffer has enough capacity
		// for the existing string including NUL terminator

		if ((CurPos + RequiredAdditionalCapacity) < End)
			return;

		Extend(RequiredAdditionalCapacity);
	}

	CORE_API void	Extend(SIZE_T extraCapacity);
	CORE_API void*	AllocBuffer(SIZE_T byteCount);
	CORE_API void	FreeBuffer(void* buffer, SIZE_T byteCount);

	C*			Base;
	C* 			CurPos;
	C*			End;
	bool		bIsDynamic = false;
	bool		bIsExtendable = false;
};

//////////////////////////////////////////////////////////////////////////

extern template class TStringBuilderImpl<ANSICHAR>;

class FAnsiStringBuilderBase : public TStringBuilderImpl<ANSICHAR>
{
public:
	operator FAnsiStringView() const { return FAnsiStringView(Base, CurPos - Base); }

protected:
	inline FAnsiStringBuilderBase(ANSICHAR* BufferPointer, SIZE_T BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FAnsiStringBuilderBase() = default;
};

template<SIZE_T N>
class TFixedAnsiStringBuilder : public FAnsiStringBuilderBase
{
public:
	inline TFixedAnsiStringBuilder()
	: FAnsiStringBuilderBase(StringBuffer, N)
	{
	}

	~TFixedAnsiStringBuilder() = default;

private:
	ANSICHAR	StringBuffer[N];
};

template<SIZE_T N>
class TAnsiStringBuilder : public FAnsiStringBuilderBase
{
public:
	inline TAnsiStringBuilder()
	: FAnsiStringBuilderBase(StringBuffer, N)
	{
		bIsExtendable = true;
	}

	~TAnsiStringBuilder() = default;

private:
	ANSICHAR	StringBuffer[N];
};

//////////////////////////////////////////////////////////////////////////

extern template class TStringBuilderImpl<WIDECHAR>;

class FWideStringBuilderBase : public TStringBuilderImpl<WIDECHAR>
{
public:
	operator FWideStringView() const { return FWideStringView(Base, CurPos - Base); }

	using TStringBuilderImpl<WIDECHAR>::Append;

	inline TStringBuilderImpl& Append(const FWideStringView& StringView)
	{
		const SIZE_T StringLength = StringView.Len();

		EnsureCapacity(StringLength);
		WIDECHAR* RESTRICT Dest = CurPos;

		FMemory::Memcpy(CurPos, StringView.Data(), StringLength * sizeof(WIDECHAR));

		CurPos += StringLength;

		return *this;
	}

protected:
	FWideStringBuilderBase(WIDECHAR* BufferPointer, size_t BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FWideStringBuilderBase() = default;
};

template<SIZE_T N>
class TFixedWideStringBuilder : public FWideStringBuilderBase
{
public:
	inline TFixedWideStringBuilder()
	: FWideStringBuilderBase(StringBuffer, N)
	{
	}

	~TFixedWideStringBuilder() = default;

private:
	WIDECHAR	StringBuffer[N];
};

template<SIZE_T N>
class TWideStringBuilder : public FWideStringBuilderBase
{
public:
	inline TWideStringBuilder()
	: FWideStringBuilderBase(StringBuffer, N)
	{
		bIsExtendable = true;
	}

	~TWideStringBuilder() = default;

private:
	WIDECHAR	StringBuffer[N];
};

// In theory, this should probably be configurable but it seems like the
// engine currently does not support narrow characters.

static_assert(sizeof(TCHAR) == sizeof(WIDECHAR), "Currently expecting wide TCHAR only");

template<SIZE_T N> using TFixedStringBuilder	= TFixedWideStringBuilder<N>;
template<SIZE_T N> using TStringBuilder			= TWideStringBuilder<N>;

// Append operator implementations

inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const WIDECHAR Char)					{ Builder.Append(Char); return Builder; }

#if USE_STRING_LITERAL_PATH

inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const WIDECHAR*& NulTerminatedString)	{ Builder.Append(NulTerminatedString); return Builder; }
inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const ANSICHAR*& NulTerminatedString)	{ Builder.AppendAnsi(NulTerminatedString); return Builder; }

template<SIZE_T N> FWideStringBuilderBase&	operator<<(FWideStringBuilderBase& Builder, const WIDECHAR (&StringLiteral)[N])		{ Builder.Append(StringLiteral); return Builder; }
template<SIZE_T N> FWideStringBuilderBase&	operator<<(FWideStringBuilderBase& Builder, const ANSICHAR (&StringLiteral)[N])		{ Builder.AppendAnsi(StringLiteral); return Builder; }

template<SIZE_T N> FWideStringBuilderBase&	operator<<(FWideStringBuilderBase& Builder, WIDECHAR (&NulTerminatedString)[N])		{ Builder.Append(&NulTerminatedString[0], TCString<WIDECHAR>::Strlen(NulTerminatedString)); return Builder; }
template<SIZE_T N> FWideStringBuilderBase&	operator<<(FWideStringBuilderBase& Builder, ANSICHAR (&NulTerminatedString)[N])		{ Builder.AppendAnsi(&NulTerminatedString[0], TCString<ANSICHAR>::Strlen(NulTerminatedString)); return Builder; }

#else

inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const WIDECHAR* NulTerminatedString)	{ Builder.Append(NulTerminatedString); return Builder; }
inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const ANSICHAR* NulTerminatedString)	{ Builder.AppendAnsi(NulTerminatedString); return Builder; }

#endif

inline FWideStringBuilderBase&				operator<<(FWideStringBuilderBase& Builder, const FWideStringView& Str)				{ Builder.Append(Str.Data(), Str.Len()); return Builder; }
