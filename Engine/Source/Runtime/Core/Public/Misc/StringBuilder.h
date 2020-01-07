// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringView.h"
#include "CoreTypes.h"
#include "Misc/CString.h"
#include "Templates/AndOrNot.h"
#include "Templates/EnableIf.h"
#include "Templates/IsArrayOrRefOfType.h"
#include "Templates/IsValidVariadicFunctionArg.h"
#include "Templates/UnrealTemplate.h"
#include "Traits/IsContiguousContainer.h"

#define USE_STRING_LITERAL_PATH 1

class FAnsiStringBuilderBase;
class FStringBuilderBase;

namespace StringBuilderPrivate
{
	template <typename CharType> struct TStringBuilderBaseType;
	template <> struct TStringBuilderBaseType<ANSICHAR> { using Type = FAnsiStringBuilderBase; };
	template <> struct TStringBuilderBaseType<WIDECHAR> { using Type = FStringBuilderBase; };
}

/** The string builder base type to be used by append operators and function output parameters for a given character type. */
template <typename CharType>
using TStringBuilderBase = typename StringBuilderPrivate::TStringBuilderBaseType<CharType>::Type;

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
	/** The character type that this builder operates on. */
	using ElementType = C;
	/** The string builder base type to be used by append operators and function output parameters. */
	using BuilderType = TStringBuilderBase<ElementType>;
	/** The string view type that this builder is compatible with. */
	using ViewType = TStringView<ElementType>;

	/** Whether the given type can be appended to this builder using the append operator. */
	template <typename AppendType>
	using TCanAppend = TIsSame<BuilderType&, decltype(DeclVal<BuilderType&>() << DeclVal<AppendType>())>;

	/** Whether the given range type can have its elements appended to the builder using the append operator. */
	template <typename RangeType>
	using TCanAppendRange = TAnd<TIsContiguousContainer<RangeType>, TCanAppend<decltype(*::GetData(DeclVal<RangeType>()))>>;

				TStringBuilderImpl() = default;
	CORE_API	~TStringBuilderImpl();

				TStringBuilderImpl(const TStringBuilderImpl&) = delete;
				TStringBuilderImpl(TStringBuilderImpl&&) = delete;

	const TStringBuilderImpl& operator=(const TStringBuilderImpl&) = delete;
	const TStringBuilderImpl& operator=(TStringBuilderImpl&&) = delete;

	inline int32	Len() const			{ return int32(CurPos - Base); }
	inline C* GetData()					{ return Base; }
	inline const C* GetData() const		{ return Base; }
	inline const C* ToString() const	{ EnsureNulTerminated(); return Base; }
	inline const C* operator*() const	{ EnsureNulTerminated(); return Base; }

	inline operator ViewType() const	{ return ViewType(Base, CurPos - Base); }

	inline const C	LastChar() const	{ return *(CurPos - 1); }

	/**
	 * Empties the string builder, but doesn't change memory allocation.
	 */
	inline void Reset()
	{
		CurPos = Base;
	}

	/**
	 * Adds a given number of uninitialized characters into the string builder.
	 *
	 * @param InCount The number of uninitialized characters to add.
	 *
	 * @return The number of characters in the string builder before adding the new characters.
	 */
	inline int32 AddUninitialized(int32 InCount)
	{
		EnsureCapacity(InCount);
		const int32 OldCount = Len();
		CurPos += InCount;
		return OldCount;
	}

	/**
	 * Modifies the string builder to remove the given number of characters from the end.
	 */
	inline void RemoveSuffix(int32 InCount)
	{
		check(InCount <= Len());
		CurPos -= InCount;
	}

	inline BuilderType& Append(C Char)
	{
		EnsureCapacity(1);

		*CurPos++ = Char;

		return BaseBuilder();
	}

#if USE_STRING_LITERAL_PATH
	inline BuilderType& AppendAnsi(const ANSICHAR*& NulTerminatedString)
#else
	inline BuilderType& AppendAnsi(const ANSICHAR* NulTerminatedString)
#endif
	{
		if (!NulTerminatedString)
		{
			return BaseBuilder();
		}

		return AppendAnsi(NulTerminatedString, TCString<ANSICHAR>::Strlen(NulTerminatedString));
	}

	inline BuilderType& AppendAnsi(const FAnsiStringView& AnsiString)
	{
		return AppendAnsi(AnsiString.GetData(), AnsiString.Len());
	}

	inline BuilderType& AppendAnsi(const ANSICHAR* String, const int32 Length)
	{
		EnsureCapacity(Length);

		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (int32 i = 0; i < Length; ++i)
		{
			Dest[i] = String[i];
		}

		return BaseBuilder();
	}

#if USE_STRING_LITERAL_PATH
	inline BuilderType& Append(const C*& NulTerminatedString)
#else
	inline BuilderType& Append(const C* NulTerminatedString)
#endif
	{
		if (!NulTerminatedString)
		{
			return BaseBuilder();
		}

		return Append(NulTerminatedString, TCString<C>::Strlen(NulTerminatedString));
	}

	inline BuilderType& Append(const ViewType& StringView)
	{
		return Append(StringView.GetData(), StringView.Len());
	}

	inline BuilderType& Append(const C* String, int32 Length)
	{
		EnsureCapacity(Length);
		C* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, String, Length * sizeof(C));

		return BaseBuilder();
	}

#if USE_STRING_LITERAL_PATH
	template<int32 ArrayLength>
	inline BuilderType& Append(C(&CharArray)[ArrayLength])
	{
		return Append(&CharArray[0]);
	}

	template<int32 LiteralLength>
	inline BuilderType& Append(const C (&StringLiteral)[LiteralLength])
	{
		constexpr int32 Length = LiteralLength - 1;
		return Append(StringLiteral, Length);
	}

	template<int32 LiteralLength>
	inline BuilderType& AppendAnsi(const ANSICHAR (&StringLiteral)[LiteralLength])
	{
		constexpr int32 Length = LiteralLength - 1;
		return AppendAnsi(StringLiteral, Length);
	}
#endif

	/**
	 * Append every element of the range to the builder, separating the elements by the delimiter.
	 *
	 * This function is only available when the elements of the range and the delimiter can both be
	 * written to the builder using the append operator.
	 *
	 * @param InRange The range of elements to join and append.
	 * @param InDelimiter The delimiter to append as a separator for the elements.
	 *
	 * @return The builder, to allow additional operations to be composed with this one.
	 */
	template <typename RangeType, typename DelimiterType,
		typename = typename TEnableIf<TAnd<TCanAppendRange<RangeType&&>, TCanAppend<DelimiterType&&>>::Value>::Type>
	inline BuilderType& Join(RangeType&& InRange, DelimiterType&& InDelimiter)
	{
		BuilderType& Self = BaseBuilder();
		bool bFirst = true;
		for (auto&& Elem : Forward<RangeType>(InRange))
		{
			if (bFirst)
			{
				bFirst = false;
			}
			else
			{
				Self << InDelimiter;
			}
			Self << Elem;
		}
		return Self;
	}

	/**
	 * Append every element of the range to the builder, separating the elements by the delimiter, and
	 * surrounding every element on each side with the given quote.
	 *
	 * This function is only available when the elements of the range, the delimiter, and the quote can be
	 * written to the builder using the append operator.
	 *
	 * @param InRange The range of elements to join and append.
	 * @param InDelimiter The delimiter to append as a separator for the elements.
	 * @param InQuote The quote to append on both sides of each element.
	 *
	 * @return The builder, to allow additional operations to be composed with this one.
	 */
	template <typename RangeType, typename DelimiterType, typename QuoteType,
		typename = typename TEnableIf<TAnd<TCanAppendRange<RangeType>, TCanAppend<DelimiterType&&>, TCanAppend<QuoteType&&>>::Value>::Type>
	inline BuilderType& JoinQuoted(RangeType&& InRange, DelimiterType&& InDelimiter, QuoteType&& InQuote)
	{
		BuilderType& Self = BaseBuilder();
		bool bFirst = true;
		for (auto&& Elem : Forward<RangeType>(InRange))
		{
			if (bFirst)
			{
				bFirst = false;
			}
			else
			{
				Self << InDelimiter;
			}
			Self << InQuote << Elem << InQuote;
		}
		return Self;
	}

	/**
	 * Appends to the string builder similarly to how classic sprintf works.
	 *
	 * @param Format A format string that specifies how to format the additional arguments. Refer to standard printf format.
	 */
	template <typename FmtType, typename... Types>
	typename TEnableIf<TIsArrayOrRefOfType<FmtType, C>::Value, BuilderType&>::Type Appendf(const FmtType& Fmt, Types... Args)
	{
		static_assert(TIsArrayOrRefOfType<FmtType, C>::Value, "Formatting string must be a character array.");
		static_assert(TAnd<TIsValidVariadicFunctionArg<Types>...>::Value, "Invalid argument(s) passed to Appendf.");
		return AppendfImpl(BaseBuilder(), Fmt, Forward<Types>(Args)...);
	}

private:
	CORE_API static BuilderType& VARARGS AppendfImpl(BuilderType& Self, const C* Fmt, ...);

	inline BuilderType& BaseBuilder() { return static_cast<BuilderType&>(*this); }

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

template <typename CharType>
struct TIsContiguousContainer<TStringBuilderImpl<CharType>> { enum { Value = true }; };

template <typename CharType>
constexpr inline SIZE_T GetNum(const TStringBuilderImpl<CharType>& Builder)
{
	return Builder.Len();
}

//////////////////////////////////////////////////////////////////////////

class FAnsiStringBuilderBase : public TStringBuilderImpl<ANSICHAR>
{
protected:
	inline FAnsiStringBuilderBase(ANSICHAR* BufferPointer, int32 BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FAnsiStringBuilderBase() = default;
};

template <>
struct TIsContiguousContainer<FAnsiStringBuilderBase> { enum { Value = true }; };

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

template <int32 N>
struct TIsContiguousContainer<TFixedAnsiStringBuilder<N>> { enum { Value = true }; };

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

template <int32 N>
struct TIsContiguousContainer<TAnsiStringBuilder<N>> { enum { Value = true }; };

//////////////////////////////////////////////////////////////////////////

class FStringBuilderBase : public TStringBuilderImpl<TCHAR>
{
protected:
	FStringBuilderBase(TCHAR* BufferPointer, int32 BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	~FStringBuilderBase() = default;
};

template <>
struct TIsContiguousContainer<FStringBuilderBase> { enum { Value = true }; };

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

template <int32 N>
struct TIsContiguousContainer<TFixedStringBuilder<N>> { enum { Value = true }; };

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

template <int32 N>
struct TIsContiguousContainer<TStringBuilder<N>> { enum { Value = true }; };

// Append operator implementations

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR Char)						{ return Builder.Append(Char); }

#if USE_STRING_LITERAL_PATH

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR*& NulTerminatedString)		{ return Builder.Append(NulTerminatedString); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const ANSICHAR*& NulTerminatedString)	{ return Builder.AppendAnsi(NulTerminatedString); }

template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, const TCHAR (&StringLiteral)[N])		{ return Builder.Append(StringLiteral); }
template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, const ANSICHAR (&StringLiteral)[N])		{ return Builder.AppendAnsi(StringLiteral); }

template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, TCHAR (&NulTerminatedString)[N])		{ return Builder.Append(&NulTerminatedString[0], TCString<TCHAR>::Strlen(NulTerminatedString)); }
template<int32 N> FStringBuilderBase&		operator<<(FStringBuilderBase& Builder, ANSICHAR (&NulTerminatedString)[N])		{ return Builder.AppendAnsi(&NulTerminatedString[0], TCString<ANSICHAR>::Strlen(NulTerminatedString)); }

#else

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const TCHAR* NulTerminatedString)		{ return Builder.Append(NulTerminatedString); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const ANSICHAR* NulTerminatedString)	{ return Builder.AppendAnsi(NulTerminatedString); }

#endif

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, const FStringView& Str)					{ return Builder.Append(Str); }

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, int32 Value)							{ return Builder.Appendf(TEXT("%d"), Value); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, uint32 Value)							{ return Builder.Appendf(TEXT("%u"), Value); }

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, int64 Value)							{ return Builder.Appendf(TEXT(INT64_FMT), Value); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, uint64 Value)							{ return Builder.Appendf(TEXT(UINT64_FMT), Value); }

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, int8 Value)								{ return Builder << int32(Value); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, uint8 Value)							{ return Builder << uint32(Value); }

inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, int16 Value)							{ return Builder << int32(Value); }
inline FStringBuilderBase&					operator<<(FStringBuilderBase& Builder, uint16 Value)							{ return Builder << uint32(Value); }

// Append operator implementations for FAnsiStringBuilderBase

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, const ANSICHAR Char)					{ return Builder.Append(Char); }

#if USE_STRING_LITERAL_PATH

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, const ANSICHAR*& NulTerminatedString)	{ return Builder.Append(NulTerminatedString); }
template<int32 N> FAnsiStringBuilderBase&	operator<<(FAnsiStringBuilderBase& Builder, const ANSICHAR (&StringLiteral)[N])		{ return Builder.Append(StringLiteral); }
template<int32 N> FAnsiStringBuilderBase&	operator<<(FAnsiStringBuilderBase& Builder, ANSICHAR (&NulTerminatedString)[N])		{ return Builder.Append(&NulTerminatedString[0], TCString<ANSICHAR>::Strlen(NulTerminatedString)); }

#else

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, const ANSICHAR* NulTerminatedString)	{ return Builder.Append(NulTerminatedString); }

#endif

template <typename T, typename = decltype(ImplicitConv<FAnsiStringView>(DeclVal<T>()))>
inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, T&& Str)								{ return Builder.Append(ImplicitConv<FAnsiStringView>(Forward<T>(Str))); }

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, int32 Value)							{ return Builder.Appendf("%d", Value); }
inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, uint32 Value)							{ return Builder.Appendf("%u", Value); }

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, int64 Value)							{ return Builder.Appendf(INT64_FMT, Value); }
inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, uint64 Value)							{ return Builder.Appendf(UINT64_FMT, Value); }

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, int8 Value)								{ return Builder << int32(Value); }
inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, uint8 Value)							{ return Builder << uint32(Value); }

inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, int16 Value)							{ return Builder << int32(Value); }
inline FAnsiStringBuilderBase&				operator<<(FAnsiStringBuilderBase& Builder, uint16 Value)							{ return Builder << uint32(Value); }
