// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/CString.h"

namespace UE::String
{

/**
 * Breaks up a delimited string into elements of a string array.
 *
 * @param	InArray		The array to fill with the string pieces. Can be an Array of Strings or StringViews.
 * @param	Text		The input string to parse.
 * @param	pchDelim	The string to delimit on. (Legacy behavior, may change): If empty, output array is empty.
 * @param	InCullEmpty	If 1, empty strings are not added to the array
 *
 * @return	The number of elements in InArray
 */
template <typename OutElementType, typename AllocatorType>
int32 ParseIntoArray(TArray<OutElementType, AllocatorType>& OutArray, FStringView Text, FStringView pchDelim, const bool InCullEmpty)
{
	OutArray.Reset();
	const TCHAR* Start = Text.GetData();
	const TCHAR* TextEnd = Text.GetData() + Text.Len();
	const int32 DelimLength = pchDelim.Len();
	int32 RemainingLen = UE_PTRDIFF_TO_INT32(TextEnd - Start);
	// TODO Legacy behavior: if DelimLength is 0 we return an empty array. Can we change this to return { Text }?
	if (Start && Start != TextEnd && DelimLength)
	{
		while (const TCHAR *At = FCString::Strnstr(Start, RemainingLen,	pchDelim.GetData(), DelimLength))
		{
			if (!InCullEmpty || At-Start)
			{
				OutArray.Emplace(FStringView(Start, UE_PTRDIFF_TO_INT32(At-Start)));
			}
			Start = At + DelimLength;
			RemainingLen = UE_PTRDIFF_TO_INT32(TextEnd - Start);
		}
		if (!InCullEmpty || Start != TextEnd)
		{
			OutArray.Emplace(FStringView(Start, RemainingLen));
		}

	}
	return OutArray.Num();
}


} // UE::String
