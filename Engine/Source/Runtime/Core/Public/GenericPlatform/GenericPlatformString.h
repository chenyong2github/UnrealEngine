// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/EnableIf.h"
#include "GenericPlatform/GenericPlatformStricmp.h"
#include <type_traits>

namespace UE::Core::Private
{
	// The Dest parameter is just used for overload resolution
	CORE_API int32 GetConvertedLength(const UTF8CHAR* Dest, const ANSICHAR* Src, int32 SrcLen);
	CORE_API int32 GetConvertedLength(const UTF8CHAR* Dest, const WIDECHAR* Src, int32 SrcLen);
	CORE_API int32 GetConvertedLength(const UTF8CHAR* Dest, const UCS2CHAR* Src, int32 SrcLen);
	CORE_API int32 GetConvertedLength(const ANSICHAR* Dest, const UTF8CHAR* Src, int32 SrcLen);
	CORE_API int32 GetConvertedLength(const WIDECHAR* Dest, const UTF8CHAR* Src, int32 SrcLen);
	CORE_API int32 GetConvertedLength(const UCS2CHAR* Dest, const UTF8CHAR* Src, int32 SrcLen);

	CORE_API UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const ANSICHAR* Src, int32 SrcLen, UTF8CHAR BogusChar);
	CORE_API UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const WIDECHAR* Src, int32 SrcLen, UTF8CHAR BogusChar);
	CORE_API UTF8CHAR* Convert(UTF8CHAR* Dest, int32 DestLen, const UCS2CHAR* Src, int32 SrcLen, UTF8CHAR BogusChar);
	CORE_API ANSICHAR* Convert(ANSICHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen, ANSICHAR BogusChar);
	CORE_API WIDECHAR* Convert(WIDECHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen, WIDECHAR BogusChar);
	CORE_API UCS2CHAR* Convert(UCS2CHAR* Dest, int32 DestLen, const UTF8CHAR* Src, int32 SrcLen, UCS2CHAR BogusChar);
}

// These will be moved inside GenericPlatformString.cpp when the platform layer handles UTF-16
// instead of StringConv.h.
#define HIGH_SURROGATE_START_CODEPOINT    ((uint16)0xD800)
#define HIGH_SURROGATE_END_CODEPOINT      ((uint16)0xDBFF)
#define LOW_SURROGATE_START_CODEPOINT     ((uint16)0xDC00)
#define LOW_SURROGATE_END_CODEPOINT       ((uint16)0xDFFF)
#define ENCODED_SURROGATE_START_CODEPOINT ((uint32)0x10000)
#define ENCODED_SURROGATE_END_CODEPOINT   ((uint32)0x10FFFF)

/**
 * Generic string implementation for most platforms
 */
struct FGenericPlatformString : public FGenericPlatformStricmp
{
	/**
	 * Tests whether a particular character is a valid member of its encoding.
	 *
	 * @param Ch The character to test.
	 * @return True if the character is a valid member of Encoding.
	 */
	template <typename Encoding>
	static bool IsValidChar(Encoding Ch)
	{
		return true;
	}


	/**
	 * Tests whether a particular character can be converted to the destination encoding.
	 *
	 * @param Ch The character to test.
	 * @return True if Ch can be encoded as a DestEncoding.
	 */
	template <typename DestEncoding, typename SourceEncoding>
	static bool CanConvertChar(SourceEncoding Ch)
	{
		return IsValidChar(Ch) && (SourceEncoding)(DestEncoding)Ch == Ch && IsValidChar((DestEncoding)Ch);
	}


	/**
	 * Returns the string representing the name of the given encoding type.
	 *
	 * @return The name of the CharType as a TCHAR string.
	 */
	template <typename Encoding>
	static const TCHAR* GetEncodingTypeName();

	static const ANSICHAR* GetEncodingName()
	{
#if PLATFORM_TCHAR_IS_4_BYTES
		return "UTF-32LE";
#else
		return "UTF-16LE";
#endif
	}

	/**
	 * True if the encoding type of the string is some form of unicode
	 */
	static const bool IsUnicodeEncoded = true;


	/**
	 * Metafunction which tests whether a given character type represents a fixed-width encoding.
	 *
	 * We need to 'forward' the metafunction to a helper because of the C++ requirement that
	 * nested structs cannot be fully specialized.  They can be partially specialized however, hence the
	 * helper function.
	 */
	template <bool Dummy, typename T>
	struct TIsFixedWidthEncoding_Helper
	{
		enum { Value = false };
	};

	template <bool Dummy> struct TIsFixedWidthEncoding_Helper<Dummy, ANSICHAR>  { enum { Value = true }; };
	template <bool Dummy> struct TIsFixedWidthEncoding_Helper<Dummy, WIDECHAR>  { enum { Value = true }; };
	template <bool Dummy> struct TIsFixedWidthEncoding_Helper<Dummy, UCS2CHAR>  { enum { Value = true }; };
	template <bool Dummy> struct TIsFixedWidthEncoding_Helper<Dummy, UTF32CHAR> { enum { Value = true }; };
#if PLATFORM_TCHAR_IS_CHAR16
	template <bool Dummy> struct TIsFixedWidthEncoding_Helper<Dummy, wchar_t>   { enum { Value = true }; };
#endif

	template <typename T>
	struct TIsFixedWidthEncoding : TIsFixedWidthEncoding_Helper<false, T>
	{
	};


	/**
	 * Metafunction which tests whether two encodings are compatible.
	 *
	 * We'll say the encodings are compatible if they're both fixed-width and have the same size.  This
	 * should be good enough and catches things like UCS2CHAR and WIDECHAR being equivalent.
	 * Specializations of this template can be provided for any other special cases.
	 * Same size is a minimum requirement.
	 */
	template <typename EncodingA, typename EncodingB>
	struct UE_DEPRECATED(5.0, "TAreEncodingsCompatible is deprecated, use IsCharEncodingCompatibleWith<SrcEncoding, DestEncoding>() instead.") TAreEncodingsCompatible
	{
		enum { Value = TIsFixedWidthEncoding<EncodingA>::Value && TIsFixedWidthEncoding<EncodingB>::Value && sizeof(EncodingA) == sizeof(EncodingB) };
	};

	/**
	 * Function which returns whether one encoding type is binary compatible with another.
	 *
	 * Unlike TAreEncodingsCompatible, this is not commutative.  For example, ANSI is compatible with
	 * UTF-8, but UTF-8 is not compatible with ANSI.
	 */
	template <typename SrcEncoding, typename DestEncoding>
	static constexpr bool IsCharEncodingCompatibleWith()
	{
		if constexpr (std::is_same_v<SrcEncoding, DestEncoding>)
		{
			return true;
		}
		else if constexpr (std::is_same_v<SrcEncoding, ANSICHAR> && std::is_same_v<DestEncoding, UTF8CHAR>)
		{
			return true;
		}
		else if constexpr (std::is_same_v<SrcEncoding, UCS2CHAR> && std::is_same_v<DestEncoding, UTF16CHAR>)
		{
			return true;
		}
		else if constexpr (std::is_same_v<SrcEncoding, WIDECHAR> && std::is_same_v<DestEncoding, UCS2CHAR>)
		{
			return true;
		}
		else if constexpr (std::is_same_v<SrcEncoding, UCS2CHAR> && std::is_same_v<DestEncoding, WIDECHAR>)
		{
			return true;
		}
#if PLATFORM_TCHAR_IS_CHAR16
		else if constexpr (std::is_same_v<SrcEncoding, WIDECHAR> && std::is_same_v<DestEncoding, wchar_t>)
		{
			return true;
		}
		else if constexpr (std::is_same_v<SrcEncoding, wchar_t> && std::is_same_v<DestEncoding, WIDECHAR>)
		{
			return true;
		}
#endif
		else
		{
			return false;
		}
	};

	/**
	 * Converts the [Src, Src+SrcSize) string range from SourceEncoding to DestEncoding and writes it to the [Dest, Dest+DestSize) range.
	 * The Src range should contain a null terminator if a null terminator is required in the output.
	 * If the Dest range is not big enough to hold the converted output, NULL is returned.  In this case, nothing should be assumed about the contents of Dest.
	 *
	 * @param Dest      The start of the destination buffer.
	 * @param DestSize  The size of the destination buffer.
	 * @param Src       The start of the string to convert.
	 * @param SrcSize   The number of Src elements to convert.
	 * @param BogusChar The char to use when the conversion process encounters a character it cannot convert.
	 * @return          A pointer to one past the last-written element.
	 */
	template <typename SourceEncoding, typename DestEncoding>
	static FORCEINLINE DestEncoding* Convert(DestEncoding* Dest, int32 DestSize, const SourceEncoding* Src, int32 SrcSize, DestEncoding BogusChar = (DestEncoding)'?')
	{
		if constexpr (IsCharEncodingCompatibleWith<SourceEncoding, DestEncoding>())
		{
			if (DestSize < SrcSize)
			{
				return nullptr;
			}

			return (DestEncoding*)Memcpy(Dest, Src, SrcSize * sizeof(SourceEncoding)) + SrcSize;
		}
		else if constexpr (TIsFixedWidthEncoding<SourceEncoding>::Value && TIsFixedWidthEncoding<DestEncoding>::Value)
		{
			const int32 Size = DestSize <= SrcSize ? DestSize : SrcSize;
			bool bInvalidChars = false;
			for (int I = 0; I < Size; ++I)
			{
				SourceEncoding SrcCh = Src[I];
				Dest[I] = (DestEncoding)SrcCh;
				bInvalidChars |= !CanConvertChar<DestEncoding>(SrcCh);
			}

			if (bInvalidChars)
			{
				for (int I = 0; I < Size; ++I)
				{
					if (!CanConvertChar<DestEncoding>(Src[I]))
					{
						Dest[I] = BogusChar;
					}
				}

				LogBogusChars<DestEncoding>(Src, Size);
			}

			return DestSize < SrcSize ? nullptr : Dest + Size;
		}
		else
		{
			return UE::Core::Private::Convert(Dest, DestSize, Src, SrcSize, BogusChar);
		}
	}


	/**
	 * Returns the required buffer length for the [Src, Src+SrcSize) string when converted to the DestChar encoding.
	 * The Src range should contain a null terminator if a null terminator is required in the output.
	 * 
	 * @param  Src     The start of the string to convert.
	 * @param  SrcSize The number of Src elements to convert.
	 * @return         The number of DestChar elements that Src will be converted into.
	 */
	template <typename DestEncoding, typename SourceEncoding>
	static int32 ConvertedLength(const SourceEncoding* Src, int32 SrcSize)
	{
		if constexpr (std::is_same_v<SourceEncoding, DestEncoding> || (TIsFixedWidthEncoding<SourceEncoding>::Value && TIsFixedWidthEncoding<DestEncoding>::Value))
		{
			return SrcSize;
		}
		else
		{
			return UE::Core::Private::GetConvertedLength((DestEncoding*)nullptr, Src, SrcSize);
		}
	}

	CORE_API static int32 Strncmp(const ANSICHAR* String1, const ANSICHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const WIDECHAR* String1, const ANSICHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const UTF8CHAR* String1, const ANSICHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const ANSICHAR* String1, const WIDECHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const WIDECHAR* String1, const WIDECHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const UTF8CHAR* String1, const WIDECHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const ANSICHAR* String1, const UTF8CHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const WIDECHAR* String1, const UTF8CHAR* String2, SIZE_T Count);
	CORE_API static int32 Strncmp(const UTF8CHAR* String1, const UTF8CHAR* String2, SIZE_T Count);

private:
	/**
	 * Forwarding function because we can't call FMemory::Memcpy directly due to #include ordering issues.
	 *
	 * @param Dest  The destination buffer.
	 * @param Src   The source buffer.
	 * @param Count The number of bytes to copy.
	 * @return      Dest
	 */
	static CORE_API void* Memcpy(void* Dest, const void* Src, SIZE_T Count);


	/**
	 * Logs a message about bogus characters which were detected during string conversion.
	 *
	 * @param Src     Pointer to the possibly-not-null-terminated string being converted.
	 * @param SrcSize Number of characters in the Src string.
	 */
	template <typename DestEncoding, typename SourceEncoding>
	static CORE_API void LogBogusChars(const SourceEncoding* Src, int32 SrcSize);
};


/**
 * Specialization of IsValidChar for ANSICHARs.
 */
template <>
inline bool FGenericPlatformString::IsValidChar<ANSICHAR>(ANSICHAR Ch)
{
	return Ch >= 0x00 && Ch <= 0x7F;
}
