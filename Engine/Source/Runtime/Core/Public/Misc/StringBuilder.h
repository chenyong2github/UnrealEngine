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
	
	This is not intended to be used as a mechanism for holding on to 
	strings for a long time - the use case is explicitly to aid in 
	*constructing* strings on the stack	and subsequently passing the 
	string into a function call or a more permanent string storage 
	mechanism like FString et al

	The amount of buffer space to allocate is specified via a template
	parameter and if the constructed string should overflow this initial
	buffer, a new buffer will be allocated using regular dynamic memory
	allocations. For instances where you absolutely must not allocate
	any memory, you should use the fixed variants named
	TFixed*StringBuilder

	Overflow allocation should be the exceptional case however -- always
	try to size the buffer so that it can hold the vast majority of 
	strings you expect to construct.

	Be mindful that stack is a limited resource, so if you are writing a
	highly recursive function you may want to use some other mechanism
	to build your strings.

  */

template<typename C>
class TStringBuilderImpl
{
public:
				TStringBuilderImpl() = default;
	CORE_API	~TStringBuilderImpl();

				TStringBuilderImpl(const TStringBuilderImpl&) = delete;
				TStringBuilderImpl(TStringBuilderImpl&&) = delete;

	const TStringBuilderImpl& operator=(const TStringBuilderImpl&) = delete;
	const TStringBuilderImpl& operator=(TStringBuilderImpl&&) = delete;

	inline int32	Len() const			{ return int32(CurPos - Base); }
	inline const C* ToString() const	{ EnsureNulTerminated(); return Base; }
	inline const C* operator*() const	{ EnsureNulTerminated(); return Base; }

	inline const C	LastChar() const	{ return *(CurPos - 1); }

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
		const int32 Length = TCString<ANSICHAR>::Strlen(NulTerminatedString);

		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (int32 i = 0; i < Length; ++i)
		{
			Dest[i] = NulTerminatedString[i];
		}

		return *this;
	}

	inline TStringBuilderImpl& AppendAnsi(const FAnsiStringView& AnsiString)
	{
		return AppendAnsi(AnsiString.GetData(), AnsiString.Len());
	}

	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR* NulTerminatedString, const int32 Length)
	{
		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (int32 i = 0; i < Length; ++i)
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
		const int32 Length = TCString<C>::Strlen(NulTerminatedString);

		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, NulTerminatedString, Length * sizeof(C));

		return *this;
	}

	inline TStringBuilderImpl& Append(const C* NulTerminatedString, int32 MaxChars)
	{
		const int32 StringLength = TCString<C>::Strlen(NulTerminatedString);
		const int32 Length = FPlatformMath::Min<int32>(MaxChars, StringLength);

		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, NulTerminatedString, Length * sizeof(C));

		return *this;
	}

#if USE_STRING_LITERAL_PATH
	template<int32 ArrayLength>
	inline TStringBuilderImpl& Append(C(&CharArray)[ArrayLength])
	{
		return Append(&CharArray[0]);
	}

	template<int32 LiteralLength>
	inline TStringBuilderImpl& Append(const C (&StringLiteral)[LiteralLength])
	{
		constexpr int32 Length = LiteralLength - 1;
		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, StringLiteral, Length * sizeof(C));

		return *this;
	}

	template<int32 LiteralLength>
	inline TStringBuilderImpl& AppendAnsi(const ANSICHAR(&StringLiteral)[LiteralLength])
	{
		constexpr int32 Length = LiteralLength - 1;
		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (int32 i = 0; i < Length; ++i)
		{
			Dest[i] = StringLiteral[i];
		}

		return *this;
	}
#endif

protected:
	inline void Initialize(C* InBase, int32 InCapacity)
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

	inline void EnsureCapacity(int32 RequiredAdditionalCapacity)
	{
		// precondition: we know the current buffer has enough capacity
		// for the existing string including NUL terminator

		if ((CurPos + RequiredAdditionalCapacity) < End)
			return;

		Extend(RequiredAdditionalCapacity);
	}

	CORE_API void	Extend(int32 extraCapacity);
	CORE_API void*	AllocBuffer(int32 byteCount);
	CORE_API void	FreeBuffer(void* buffer, int32 byteCount);

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
	inline FAnsiStringBuilderBase(ANSICHAR* BufferPointer, int32 BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FAnsiStringBuilderBase() = default;
};

template<int32 N>
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

template<int32 N>
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

extern template class TStringBuilderImpl<TCHAR>;

class FStringBuilderBase : public TStringBuilderImpl<TCHAR>
{
public:
	operator FStringView() const { return FStringView(Base, CurPos - Base); }

	using TStringBuilderImpl<TCHAR>::Append;

	inline TStringBuilderImpl& Append(const FStringView& StringView)
	{
		const int32 StringLength = StringView.Len();

		EnsureCapacity(StringLength);
		TCHAR* RESTRICT Dest = CurPos;

		FMemory::Memcpy(CurPos, StringView.GetData(), StringLength * sizeof(TCHAR));

		CurPos += StringLength;

		return *this;
	}

protected:
	FStringBuilderBase(TCHAR* BufferPointer, int32 BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FStringBuilderBase() = default;
};

template<int32 N>
class TFixedStringBuilder : public FStringBuilderBase
{
public:
	inline TFixedStringBuilder()
	: FStringBuilderBase(StringBuffer, N)
	{
	}

	~TFixedStringBuilder() = default;

private:
	TCHAR	StringBuffer[N];
};

template<int32 N>
class TStringBuilder : public FStringBuilderBase
{
public:
	inline TStringBuilder()
	: FStringBuilderBase(StringBuffer, N)
	{
		bIsExtendable = true;
	}

	~TStringBuilder() = default;

private:
	TCHAR	StringBuffer[N];
};

// Append operator implementations

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR Char)						{ Builder.Append(Char); return Builder; }

#if USE_STRING_LITERAL_PATH

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR*& NulTerminatedString)		{ Builder.Append(NulTerminatedString); return Builder; }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const ANSICHAR*& NulTerminatedString)	{ Builder.AppendAnsi(NulTerminatedString); return Builder; }

template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, const TCHAR (&StringLiteral)[N])		{ Builder.Append(StringLiteral); return Builder; }
template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, const ANSICHAR (&StringLiteral)[N])		{ Builder.AppendAnsi(StringLiteral); return Builder; }

template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, TCHAR (&NulTerminatedString)[N])		{ Builder.Append(&NulTerminatedString[0], TCString<TCHAR>::Strlen(NulTerminatedString)); return Builder; }
template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, ANSICHAR (&NulTerminatedString)[N])		{ Builder.AppendAnsi(&NulTerminatedString[0], TCString<ANSICHAR>::Strlen(NulTerminatedString)); return Builder; }

#else

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR* NulTerminatedString)		{ Builder.Append(NulTerminatedString); return Builder; }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const ANSICHAR* NulTerminatedString)	{ Builder.AppendAnsi(NulTerminatedString); return Builder; }

#endif

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const FStringView& Str)					{ Builder.Append(Str.GetData(), Str.Len()); return Builder; }
