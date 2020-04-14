// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// String Builder

template <typename CharType> class TStringBuilderBase;
template <typename CharType, int32 BufferSize> class TStringBuilderWithBuffer;

/** The base string builder type for TCHAR. */
using FStringBuilderBase = TStringBuilderBase<TCHAR>;
/** The base string builder type for ANSICHAR. */
using FAnsiStringBuilderBase = TStringBuilderBase<ANSICHAR>;
/** The base string builder type for WIDECHAR. */
using FWideStringBuilderBase = TStringBuilderBase<WIDECHAR>;

/** An extendable string builder for TCHAR. */
template <int32 BufferSize> using TStringBuilder = TStringBuilderWithBuffer<TCHAR, BufferSize>;
/** An extendable string builder for ANSICHAR. */
template <int32 BufferSize> using TAnsiStringBuilder = TStringBuilderWithBuffer<ANSICHAR, BufferSize>;
/** An extendable string builder for WIDECHAR. */
template <int32 BufferSize> using TWideStringBuilder = TStringBuilderWithBuffer<WIDECHAR, BufferSize>;

/** A fixed-size string builder for TCHAR. */
template <int32 BufferSize> using TFixedStringBuilder UE_DEPRECATED(4.25, "'TFixedStringBuilder' is deprecated. Please use 'TStringBuilder' instead!") = TStringBuilderWithBuffer<TCHAR, BufferSize>;
/** A fixed-size string builder for ANSICHAR. */
template <int32 BufferSize> using TFixedAnsiStringBuilder UE_DEPRECATED(4.25, "'TFixedAnsiStringBuilder' is deprecated. Please use 'TAnsiStringBuilder' instead!") = TStringBuilderWithBuffer<ANSICHAR, BufferSize>;

// String View

class FStringView;
class FAnsiStringView;
using FWideStringView = FStringView;
