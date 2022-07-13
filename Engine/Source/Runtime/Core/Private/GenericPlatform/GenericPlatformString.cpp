// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformString.h"
#include "HAL/UnrealMemory.h"
#include "Misc/Char.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"


DEFINE_LOG_CATEGORY_STATIC(LogGenericPlatformString, Log, All);

template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<ANSICHAR>() { return TEXT("ANSICHAR"); }
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<WIDECHAR>() { return TEXT("WIDECHAR"); }
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<UCS2CHAR>() { return TEXT("UCS2CHAR"); }
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<UTF8CHAR>() { return TEXT("UTF8CHAR"); }
#if PLATFORM_TCHAR_IS_CHAR16
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<wchar_t>() { return TEXT("WCHAR_T"); }
#endif

void* FGenericPlatformString::Memcpy(void* Dest, const void* Src, SIZE_T Count)
{
	return FMemory::Memcpy(Dest, Src, Count);
}

namespace
{
	void TrimStringAndLogBogusCharsError(FString& SrcStr, const TCHAR* SourceCharName, const TCHAR* DestCharName)
	{
		SrcStr.TrimStartInline();
		// @todo: Put this back in once GLog becomes a #define, or is replaced with GLog::GetLog()
		//UE_LOG(LogGenericPlatformString, Warning, TEXT("Bad chars found when trying to convert \"%s\" from %s to %s"), *SrcStr, SourceCharName, DestCharName);
	}
}

namespace UE::Core::Private
{
	/**
	 * This is a basic object which counts how many times it has been incremented
	 */
	template <typename DestType>
	struct TCountingOutputIterator
	{
		TCountingOutputIterator()
			: Counter(0)
		{
		}

		const TCountingOutputIterator& operator* () const { return *this; }
		const TCountingOutputIterator& operator++() { ++Counter; return *this; }
		const TCountingOutputIterator& operator++(int) { ++Counter; return *this; }
		const TCountingOutputIterator& operator+=(const int32 Amount) { Counter += Amount; return *this; }

		const DestType& operator=(const DestType& Val) const
		{
			return Val;
		}

		friend int32 operator-(TCountingOutputIterator Lhs, TCountingOutputIterator Rhs)
		{
			return Lhs.Counter - Rhs.Counter;
		}

		int32 GetCount() const { return Counter; }

	private:
		int32 Counter;
	};

	/** Is the provided Codepoint within the range of valid codepoints? */
	FORCEINLINE bool IsValidCodepoint(const uint32 Codepoint)
	{
		if ((Codepoint > 0x10FFFF) ||						// No Unicode codepoints above 10FFFFh, (for now!)
			(Codepoint == 0xFFFE) || (Codepoint == 0xFFFF)) // illegal values.
		{
			return false;
		}
		return true;
	}

	/** Is the provided Codepoint within the range of the high-surrogates? */
	static FORCEINLINE bool IsHighSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= HIGH_SURROGATE_START_CODEPOINT && Codepoint <= HIGH_SURROGATE_END_CODEPOINT;
	}

	/** Is the provided Codepoint within the range of the low-surrogates? */
	static FORCEINLINE bool IsLowSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= LOW_SURROGATE_START_CODEPOINT && Codepoint <= LOW_SURROGATE_END_CODEPOINT;
	}

	static FORCEINLINE uint32 EncodeSurrogate(const uint16 HighSurrogate, const uint16 LowSurrogate)
	{
		return ((HighSurrogate - HIGH_SURROGATE_START_CODEPOINT) << 10) + (LowSurrogate - LOW_SURROGATE_START_CODEPOINT) + 0x10000;
	}

	static FORCEINLINE void DecodeSurrogate(const uint32 Codepoint, uint16& OutHighSurrogate, uint16& OutLowSurrogate)
	{
		const uint32 TmpCodepoint = Codepoint - 0x10000;
		OutHighSurrogate = (uint16)((TmpCodepoint >> 10) + HIGH_SURROGATE_START_CODEPOINT);
		OutLowSurrogate = (TmpCodepoint & 0x3FF) + LOW_SURROGATE_START_CODEPOINT;
	}

	/** Is the provided Codepoint outside of the range of the basic multilingual plane, but within the valid range of UTF8/16? */
	static FORCEINLINE bool IsEncodedSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= ENCODED_SURROGATE_START_CODEPOINT && Codepoint <= ENCODED_SURROGATE_END_CODEPOINT;
	}

	/**
	 * Convert TCHAR Codepoint into UTF-8 characters.
	 *
	 * @param Codepoint Codepoint to expand into UTF-8 bytes
	 * @param OutputIterator Output iterator to write UTF-8 bytes into
	 * @param OutputIteratorByteSizeRemaining Maximum number of ANSI characters that can be written to OutputIterator
	 * @return true if some data was written, false otherwise.
	 */
	template <typename BufferType>
	static bool WriteCodepointToBuffer(uint32 Codepoint, BufferType& OutputIterator, int32& OutputIteratorByteSizeRemaining)
	{
		// Ensure we have at least one character in size to write
		if (OutputIteratorByteSizeRemaining == 0)
		{
			return false;
		}

		if (!IsValidCodepoint(Codepoint))
		{
			Codepoint = (uint32)UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else if (IsHighSurrogate(Codepoint) || IsLowSurrogate(Codepoint)) // UTF-8 Characters are not allowed to encode codepoints in the surrogate pair range
		{
			Codepoint = (uint32)UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		// Do the encoding...
		if (Codepoint < 0x80)
		{
			*OutputIterator++ = (UTF8CHAR)Codepoint;

			OutputIteratorByteSizeRemaining -= 1;
		}
		else if (Codepoint < 0x800)
		{
			if (OutputIteratorByteSizeRemaining < 2)
			{
				return false;
			}

			*OutputIterator++ = (UTF8CHAR)((Codepoint >> 6)         | 128 | 64);
			*OutputIterator++ = (UTF8CHAR)((Codepoint       & 0x3F) | 128);

			OutputIteratorByteSizeRemaining -= 2;
		}
		else if (Codepoint < 0x10000)
		{
			if (OutputIteratorByteSizeRemaining < 3)
			{
				return false;
			}

			*OutputIterator++ = (UTF8CHAR)( (Codepoint >> 12)        | 128 | 64 | 32);
			*OutputIterator++ = (UTF8CHAR)(((Codepoint >> 6) & 0x3F) | 128);
			*OutputIterator++ = (UTF8CHAR)( (Codepoint       & 0x3F) | 128);

			OutputIteratorByteSizeRemaining -= 3;
		}
		else
		{
			if (OutputIteratorByteSizeRemaining < 4)
			{
				return false;
			}

			*OutputIterator++ = (UTF8CHAR)( (Codepoint >> 18)         | 128 | 64 | 32 | 16);
			*OutputIterator++ = (UTF8CHAR)(((Codepoint >> 12) & 0x3F) | 128);
			*OutputIterator++ = (UTF8CHAR)(((Codepoint >> 6 ) & 0x3F) | 128);
			*OutputIterator++ = (UTF8CHAR)( (Codepoint        & 0x3F) | 128);

			OutputIteratorByteSizeRemaining -= 4;
		}

		return true;
	}

	template <typename DestBufferType, typename FromType>
	static int32 ConvertToUTF8(DestBufferType& Dest, int32 DestLen, const FromType* Source, int32 SourceLen)
	{
		if constexpr (sizeof(FromType) == 4)
		{
			DestBufferType DestStartingPosition = Dest;

			for (;;)
			{
				if (SourceLen <= 0)
				{
					return UE_PTRDIFF_TO_INT32(Dest - DestStartingPosition);
				}

				uint32 Codepoint = static_cast<uint32>(*Source++);
				--SourceLen;

				if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
				{
					// Could not write data, bail out
					return -1;
				}
			}
		}
		else
		{
			DestBufferType DestStartingPosition = Dest;

			for (;;)
			{
				if (SourceLen <= 0)
				{
					return UE_PTRDIFF_TO_INT32(Dest - DestStartingPosition);
				}

				uint32 Codepoint = static_cast<uint32>(*Source++);
				--SourceLen;

				// Check if this character is a high-surrogate
				if (IsHighSurrogate(Codepoint))
				{
					if (SourceLen == 0)
					{
						// String ends with lone high-surrogate - write it out (will be converted into bogus character)
						if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
						{
							// Could not write data, bail out
							return -1;
						}

						return UE_PTRDIFF_TO_INT32(Dest - DestStartingPosition);
					}

					// Read next codepoint
					uint32 NextCodepoint = static_cast<uint32>(*Source);

					// If it's a low surrogate, combine it with the current high surrogate,
					// otherwise just leave the high surrogate to be written out by itself (as a bogus character)
					if (IsLowSurrogate(NextCodepoint))
					{
						Codepoint = EncodeSurrogate((uint16)Codepoint, (uint16)NextCodepoint);
						++Source;
						--SourceLen;
					}
				}

				if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
				{
					// Could not write data, bail out
					return -1;
				}
			}
		}
	}

	static uint32 CodepointFromUtf8(const UTF8CHAR*& SourceString, const uint32 SourceLengthRemaining)
	{
		checkSlow(SourceLengthRemaining > 0);

		const UTF8CHAR* OctetPtr = SourceString;

		uint32 Codepoint = 0;
		uint32 Octet = (uint32) ((uint8) *SourceString);
		uint32 Octet2, Octet3, Octet4;

		if (Octet < 128)  // one octet char: 0 to 127
		{
			++SourceString;  // read all octets - skip to next possible start of codepoint.

			Codepoint = Octet;
		}
		else if (Octet < 192)  // bad (starts with 10xxxxxx).
		{
			// Apparently each of these is supposed to be flagged as a bogus
			//  char, instead of just resyncing to the next valid codepoint.

			++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else if (Octet < 224)  // two octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 2)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128 + 64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 2;  // read all octets - skip to next possible start of codepoint.

			Codepoint = ((Octet << 6) | (Octet2 - 128));
			if (Codepoint < 0x80 || Codepoint > 0x7FF)
			{
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}
		}
		else if (Octet < 240)  // three octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 3)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64+32);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet3 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet3 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 3;  // read all octets - skip to next possible start of codepoint.

			Codepoint = ( ((Octet << 12)) | ((Octet2-128) << 6) | ((Octet3-128)) );

			// UTF-8 characters cannot be in the UTF-16 surrogates range.  0xFFFE and 0xFFFF are illegal, too, so we check them at the edge.
			if (Codepoint < 0x800 || Codepoint > 0xFFFD || IsHighSurrogate(Codepoint) || IsLowSurrogate(Codepoint))
			{
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}
		}
		else if (Octet < 248)  // four octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 4)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64+32+16);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet3 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet3 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet4 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet4 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 4;  // read all octets - skip to next possible start of codepoint.

			Codepoint = ( ((Octet << 18)) | ((Octet2 - 128) << 12) |
						((Octet3 - 128) << 6) | ((Octet4 - 128)) );
			if (Codepoint < 0x10000 || Codepoint > 0x10FFFF)
			{
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}
		}
		// Five and six octet sequences became illegal in rfc3629.
		//  We throw the codepoint away, but parse them to make sure we move
		//  ahead the right number of bytes and don't overflow the buffer.
		else if (Octet < 252)  // five octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 5)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 5;  // read all octets - skip to next possible start of codepoint.

			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		else  // six octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 6)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 6;  // read all octets - skip to next possible start of codepoint.

			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		// Should only reach here after parsing a legal codepoint
		return Codepoint;
	}

	/**
	 * Read Source string, converting the data from UTF-8 into UTF-16, and placing these in the Destination
	 */
	template <typename DestType, typename DestBufferType>
	static int32 ConvertFromUTF8(DestBufferType& ConvertedBuffer, int32 DestLen, const UTF8CHAR* Source, const int32 SourceLen)
	{
		DestBufferType DestStartingPosition = ConvertedBuffer;

		const UTF8CHAR* SourceEnd = Source + SourceLen;

		const uint64 ExtendedCharMask = 0x8080808080808080;
		while (Source < SourceEnd)
		{
			if (DestLen == 0)
			{
				return -1;
			}

			// In case we're given an unaligned pointer, we'll
			// fallback to the slow path until properly aligned.
			if (IsAligned(Source, 8))
			{
				// Fast path for most common case
				while (Source < SourceEnd - 8 && DestLen >= 8)
				{
					// Detect any extended characters 8 chars at a time
					if ((*(const uint64*)Source) & ExtendedCharMask)
					{
						// Move to slow path since we got extended characters to process
						break;
					}

					// This should get unrolled on most compiler
					// ROI of diminished return to vectorize this as we 
					// would have to deal with alignment, endianness and
					// rewrite the iterators to support bulk writes
					for (int32 Index = 0; Index < 8; ++Index)
					{
						*(ConvertedBuffer++) = (DestType)(uint8)*(Source++);
					}
					DestLen -= 8;
				}
			}

			// Slow path for extended characters
			while (Source < SourceEnd && DestLen > 0)
			{
				// Read our codepoint, advancing the source pointer
				uint32 Codepoint = CodepointFromUtf8(Source, UE_PTRDIFF_TO_UINT32(SourceEnd - Source));

				if constexpr (sizeof(DestType) != 4)
				{
					// We want to write out two chars
					if (IsEncodedSurrogate(Codepoint))
					{
						// We need two characters to write the surrogate pair
						if (DestLen >= 2)
						{
							uint16 HighSurrogate = 0;
							uint16 LowSurrogate = 0;
							DecodeSurrogate(Codepoint, HighSurrogate, LowSurrogate);

							*(ConvertedBuffer++) = (DestType)HighSurrogate;
							*(ConvertedBuffer++) = (DestType)LowSurrogate;
							DestLen -= 2;
							continue;
						}

						// If we don't have space, write a bogus character instead (we should have space for it)
						Codepoint = (uint32)UNICODE_BOGUS_CHAR_CODEPOINT;
					}
					else if (Codepoint > ENCODED_SURROGATE_END_CODEPOINT)
					{
						// Ignore values higher than the supplementary plane range
						Codepoint = (uint32)UNICODE_BOGUS_CHAR_CODEPOINT;
					}
				}

				*(ConvertedBuffer++) = (DestType)Codepoint;
				--DestLen;

				// Return to the fast path once aligned and back to simple ASCII chars
				if (Codepoint < 128 && IsAligned(Source, 8))
				{
					break;
				}
			}
		}

		return UE_PTRDIFF_TO_INT32(ConvertedBuffer - DestStartingPosition);;
	}

	/**
	 * Determines the length of the converted string.
	 *
	 * @return The length of the string in UTF-16 code units.
	 */
	int32 GetConvertedLength(const UTF8CHAR*, const ANSICHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<UTF8CHAR> Dest;
		int32 Result = ConvertToUTF8(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const UTF8CHAR*, const WIDECHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<UTF8CHAR> Dest;
		int32 Result = ConvertToUTF8(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const UTF8CHAR*, const UCS2CHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<UTF8CHAR> Dest;
		int32 Result = ConvertToUTF8(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const UTF8CHAR*, const UTF32CHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<UTF8CHAR> Dest;
		int32 Result = ConvertToUTF8(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const ANSICHAR*, const UTF8CHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<ANSICHAR> Dest;
		int32 Result = ConvertFromUTF8<ANSICHAR>(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const WIDECHAR*, const UTF8CHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<WIDECHAR> Dest;
		int32 Result = ConvertFromUTF8<WIDECHAR>(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}
	int32 GetConvertedLength(const UCS2CHAR*, const UTF8CHAR* Source, int32 SourceLen)
	{
		TCountingOutputIterator<UCS2CHAR> Dest;
		int32 Result = ConvertFromUTF8<UCS2CHAR>(Dest, INT32_MAX, Source, SourceLen);
		return Result;
	}

	UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const ANSICHAR* Src, int32 SrcLen)
	{
		if (ConvertToUTF8(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const WIDECHAR* Src, int32 SrcLen)
	{
		if (ConvertToUTF8(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const UCS2CHAR* Src, int32 SrcLen)
	{
		if (ConvertToUTF8(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const UTF32CHAR* Src, int32 SrcLen)
	{
		if (ConvertToUTF8(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	ANSICHAR* Convert(ANSICHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen)
	{
		if (ConvertFromUTF8<ANSICHAR>(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	WIDECHAR* Convert(WIDECHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen)
	{
		if (ConvertFromUTF8<WIDECHAR>(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
	UCS2CHAR* Convert(UCS2CHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen)
	{
		if (ConvertFromUTF8<UCS2CHAR>(Dest, DestLen, Src, SrcLen) == -1)
		{
			return nullptr;
		}
		return Dest;
	}
}

template <typename DestEncoding, typename SourceEncoding>
void FGenericPlatformString::LogBogusChars(const SourceEncoding* Src, int32 SrcSize)
{
	static_assert(TIsFixedWidthCharEncoding_V<SourceEncoding>, "Currently unimplemented for non-fixed-width source conversions");

	FString SrcStr;
	bool    bFoundBogusChars = false;
	for (; SrcSize; --SrcSize)
	{
		SourceEncoding SrcCh = *Src++;
		if (!CanConvertCodepoint<DestEncoding>(SrcCh))
		{
			SrcStr += FString::Printf(TEXT("[0x%X]"), (int32)SrcCh);
			bFoundBogusChars = true;
		}
		else if (CanConvertCodepoint<TCHAR>(SrcCh))
		{
			if (TChar<SourceEncoding>::IsLinebreak(SrcCh))
			{
				if (bFoundBogusChars)
				{
					TrimStringAndLogBogusCharsError(SrcStr, GetEncodingTypeName<SourceEncoding>(), GetEncodingTypeName<DestEncoding>());
					bFoundBogusChars = false;
				}
				SrcStr.Empty();
			}
			else
			{
				SrcStr.AppendChar((TCHAR)SrcCh);
			}
		}
		else
		{
			SrcStr.AppendChar((TCHAR)'?');
		}
	}

	if (bFoundBogusChars)
	{
		TrimStringAndLogBogusCharsError(SrcStr, GetEncodingTypeName<SourceEncoding>(), GetEncodingTypeName<DestEncoding>());
	}
}

namespace GenericPlatformStringPrivate
{

template<typename CharType1, typename CharType2>
int32 StrncmpImpl(const CharType1* String1, const CharType2* String2, SIZE_T Count)
{
	for (; Count > 0; --Count)
	{
		CharType1 C1 = *String1++;
		CharType2 C2 = *String2++;

		if (C1 != C2)
		{
			return TChar<CharType1>::ToUnsigned(C1) - TChar<CharType2>::ToUnsigned(C2);
		}
		if (C1 == 0)
		{
			return 0;
		}
	}

	return 0;
}

}

int32 FGenericPlatformString::Strncmp(const ANSICHAR* Str1, const ANSICHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const WIDECHAR* Str1, const ANSICHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const UTF8CHAR* Str1, const ANSICHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const ANSICHAR* Str1, const WIDECHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const WIDECHAR* Str1, const WIDECHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const UTF8CHAR* Str1, const WIDECHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const ANSICHAR* Str1, const UTF8CHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const WIDECHAR* Str1, const UTF8CHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const UTF8CHAR* Str1, const UTF8CHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }

#if !UE_BUILD_DOCS
template CORE_API void FGenericPlatformString::LogBogusChars<ANSICHAR, WIDECHAR>(const WIDECHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<ANSICHAR, UCS2CHAR>(const UCS2CHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<WIDECHAR, ANSICHAR>(const ANSICHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<WIDECHAR, UCS2CHAR>(const UCS2CHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UCS2CHAR, ANSICHAR>(const ANSICHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UCS2CHAR, WIDECHAR>(const WIDECHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UTF8CHAR, ANSICHAR>(const ANSICHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UTF8CHAR, WIDECHAR>(const WIDECHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UTF8CHAR, UCS2CHAR>(const UCS2CHAR* Src, int32 SrcSize);
#if PLATFORM_TCHAR_IS_CHAR16
template CORE_API void FGenericPlatformString::LogBogusChars<wchar_t, char16_t>(const char16_t* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<char16_t, wchar_t>(const wchar_t* Src, int32 SrcSize);
#endif
#endif
