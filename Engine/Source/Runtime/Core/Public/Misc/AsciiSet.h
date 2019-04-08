// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Char.h"

/**
 * ASCII character bitset useful for fast and readable parsing
 *
 * Entirely constexpr. Works with both wide and narrow strings.
 *
 * Example use cases:
 *
 *   constexpr FAsciiSet WhitespaceCharacters(" \v\f\t\r\n");
 *   bool bIsWhitespace = WhitespaceCharacters.Contains(MyChar);
 *   const char* HelloWorld = FAsciiSet::Skip("  \t\tHello world!", WhitespaceCharacters;
 *
 *   constexpr FAsciiSet XmlEscapeChars("&<>\"'");
 *   check(FAsciiSet::HasNone(EscapedXmlString, XmlEscapeChars));
 */
class FAsciiSet
{
public:
	template<typename CharType, int N>
	constexpr FAsciiSet(const CharType(&Chars)[N])
		: FAsciiSet(StringToBitset(Chars))
	{}

	/** Returns non-zero if a character is part of the set */
	template<typename CharType>
	constexpr FORCEINLINE uint64 Test(CharType Char) const
	{
		return TestImpl(TChar<CharType>::ToUnsigned(Char));
	}

	/** Create new set with specified character in it */
	constexpr FORCEINLINE FAsciiSet operator+(char Char) const
	{
		InitData Bitset = { LoMask, HiMask };
		Set(Bitset, Char);
		return FAsciiSet(Bitset);
	}

	/** Create new set containg inverse set of characters - likely including null-terminator */
	constexpr FORCEINLINE FAsciiSet operator~() const
	{
		return FAsciiSet(~LoMask, ~HiMask);
	}

	// C string util functions

	/** Scan string for first character in a set or null-terminator */
	template<class CharType>
	static constexpr const CharType* Find(const CharType* Str, FAsciiSet Set)
	{
		for (FAsciiSet SetOrNil(Set.LoMask | NilMask, Set.HiMask); !SetOrNil.Test(*Str); ++Str);

		return Str;
	}

	/** Scan string for first character not in a set or null-terminator */
	template<typename CharType>
	static constexpr const CharType* Skip(const CharType* Str, FAsciiSet Set)
	{
		return Find(Str, ~Set);
	}

	/** Test if string contains any character in set */
	template<typename CharType>
	static constexpr bool HasAny(const CharType* Str, FAsciiSet Set)
	{
		return *Find(Str, Set) != '\0';
	}

	/** Test if string contains no character in set */
	template<typename CharType>
	static constexpr bool HasNone(const CharType* Str, FAsciiSet Set)
	{
		return *Find(Str, Set) == '\0';
	}

	/** Test if string contains any character outside of set */
	template<typename CharType>
	static constexpr bool HasOnly(const CharType* Str, FAsciiSet Set)
	{
		return *Skip(Str, Set) == '\0';
	}

private:
	// Work-around for constexpr limitations
	struct InitData { uint64 Lo, Hi; };
	static constexpr uint64 NilMask = uint64(1) << '\0';

	template<typename CharType>
	static constexpr FORCEINLINE void Set(InitData& Bitset, CharType Char)
	{
		uint64 IsLo = uint64(0) - (Char >> 6 == 0);
		uint64 IsHi = uint64(0) - (Char >> 6 == 1);
		uint64 Bit = uint64(1) << uint8(Char & 0x3f);

		Bitset.Lo |= Bit & IsLo;
		Bitset.Hi |= Bit & IsHi;
	}

	constexpr FORCEINLINE uint64 TestImpl(uint32 Char) const
	{
		uint64 IsLo = uint64(0) - (Char >> 6 == 0);
		uint64 IsHi = uint64(0) - (Char >> 6 == 1);
		uint64 Bit = uint64(1) << (Char & 0x3f);

		return (Bit & IsLo & LoMask) | (Bit & IsHi & HiMask);
	}

	template<typename CharType, int N>
	static constexpr InitData StringToBitset(const CharType(&Chars)[N])
	{
		InitData Bitset = { 0, 0 };
		for (int I = 0; I < N - 1; ++I)
		{
			Set(Bitset, Chars[I]);
		}

		return Bitset;
	}

	constexpr FAsciiSet(InitData Bitset) 
		: LoMask(Bitset.Lo), HiMask(Bitset.Hi)
	{}

	constexpr FAsciiSet(uint64 Lo, uint64 Hi)
		: LoMask(Lo), HiMask(Hi)
	{}

	uint64 LoMask, HiMask;
};
