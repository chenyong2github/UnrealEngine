// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseTokens.h"

#include "Algo/AllOf.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Algo/Transform.h"
#include "Containers/Array.h"
#include "Containers/BitArray.h"

namespace UE::String
{

inline static void ParseTokensVisitToken(
	const TFunctionRef<void (FStringView)>& Visitor,
	const EParseTokensOptions Options,
	FStringView Token)
{
	if (EnumHasAnyFlags(Options, EParseTokensOptions::Trim))
	{
		Token = Token.TrimStartAndEnd();
	}
	if (!EnumHasAnyFlags(Options, EParseTokensOptions::SkipEmpty) || !Token.IsEmpty())
	{
		Visitor(Token);
	}
}

/** Parse tokens with one single-character delimiter. */
inline static void ParseTokens1Delim1Char(
	const FStringView View,
	const TCHAR Delimiter,
	const TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	const TCHAR* ViewIt = View.GetData();
	const TCHAR* const ViewEnd = ViewIt + View.Len();
	const TCHAR* NextToken = ViewIt;

	if (EnumHasAnyFlags(Options, EParseTokensOptions::IgnoreCase))
	{
		const TCHAR LowerDelimiter = FChar::ToLower(Delimiter);
		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (FChar::ToLower(*ViewIt) != Delimiter)
			{
				++ViewIt;
				continue;
			}
			ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}
	}
	else
	{
		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (*ViewIt != Delimiter)
			{
				++ViewIt;
				continue;
			}
			ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}
	}

	ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
}

/** Parse tokens with multiple single-character Basic Latin delimiters. */
inline static void ParseTokensNDelim1CharBasicLatin(
	const FStringView View,
	const TConstArrayView<TCHAR> Delimiters,
	const TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	TBitArray<> DelimiterMask(false, 128);
	if (EnumHasAnyFlags(Options, EParseTokensOptions::IgnoreCase))
	{
		for (TCHAR Delimiter : Delimiters)
		{
			DelimiterMask[FChar::ToUnsigned(FChar::ToLower(Delimiter))] = true;
			DelimiterMask[FChar::ToUnsigned(FChar::ToUpper(Delimiter))] = true;
		}
	}
	else
	{
		for (TCHAR Delimiter : Delimiters)
		{
			DelimiterMask[FChar::ToUnsigned(Delimiter)] = true;
		}
	}

	const TCHAR* ViewIt = View.GetData();
	const TCHAR* const ViewEnd = ViewIt + View.Len();
	const TCHAR* NextToken = ViewIt;

	for (;;)
	{
		if (ViewIt == ViewEnd)
		{
			break;
		}
		const uint32 CodePoint = *ViewIt;
		if (CodePoint >= 128 || !DelimiterMask[CodePoint])
		{
			++ViewIt;
			continue;
		}
		ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
		NextToken = ++ViewIt;
	}

	ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
}

/** Parse tokens with multiple single-character delimiters in the Basic Multilingual Plane. */
inline static void ParseTokensNDelim1Char(
	const FStringView View,
	const TConstArrayView<TCHAR> Delimiters,
	TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	if (Algo::AllOf(Delimiters, [](TCHAR Delimiter) { return Delimiter < 128; }))
	{
		return ParseTokensNDelim1CharBasicLatin(View, Delimiters, MoveTemp(Visitor), Options);
	}

	const TCHAR* ViewIt = View.GetData();
	const TCHAR* const ViewEnd = ViewIt + View.Len();
	const TCHAR* NextToken = ViewIt;

	if (EnumHasAnyFlags(Options, EParseTokensOptions::IgnoreCase))
	{
		TArray<TCHAR, TInlineAllocator<16>> LowerDelimiters;
		Algo::Transform(Delimiters, LowerDelimiters, FChar::ToLower);
		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (!Algo::Find(Delimiters, FChar::ToLower(*ViewIt)))
			{
				++ViewIt;
				continue;
			}
			ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}
	}
	else
	{
		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (!Algo::Find(Delimiters, *ViewIt))
			{
				++ViewIt;
				continue;
			}
			ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}
	}

	ParseTokensVisitToken(Visitor, Options, FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
}

/** Parse tokens with multiple multi-character delimiters. */
inline static void ParseTokensNDelimNChar(
	const FStringView View,
	const TConstArrayView<FStringView> Delimiters,
	const TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	// This is a naive implementation that takes time proportional to View.Len() * TotalDelimiterLen.
	// If this function becomes a bottleneck, it can be specialized separately for one and many delimiters.
	// There are algorithms for each are linear or sub-linear in the length of string to search.

	const FStringView::SizeType ViewLen = View.Len();
	FStringView::SizeType NextTokenIndex = 0;

	const ESearchCase::Type SearchCase = EnumHasAnyFlags(Options, EParseTokensOptions::IgnoreCase) ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive;
	for (FStringView::SizeType ViewIndex = 0; ViewIndex != ViewLen;)
	{
		const FStringView RemainingView(View.GetData() + ViewIndex, ViewLen - ViewIndex);
		auto MatchDelimiter = [RemainingView, SearchCase](FStringView Delimiter) { return RemainingView.StartsWith(Delimiter, SearchCase); };
		if (const FStringView* Delimiter = Algo::FindByPredicate(Delimiters, MatchDelimiter))
		{
			ParseTokensVisitToken(Visitor, Options, FStringView(View.GetData() + NextTokenIndex, ViewIndex - NextTokenIndex));
			ViewIndex += Delimiter->Len();
			NextTokenIndex = ViewIndex;
		}
		else
		{
			++ViewIndex;
		}
	}

	ParseTokensVisitToken(Visitor, Options, FStringView(View.GetData() + NextTokenIndex, ViewLen - NextTokenIndex));
}

void ParseTokensMultiple(
	const FStringView View,
	const TConstArrayView<FStringView> Delimiters,
	TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	check(Algo::NoneOf(Delimiters, &FStringView::IsEmpty));
	switch (Delimiters.Num())
	{
	case 0:
		return ParseTokensVisitToken(Visitor, Options, View);
	case 1:
		if (Delimiters[0].Len() == 1)
		{
			return ParseTokens1Delim1Char(View, Delimiters[0][0], MoveTemp(Visitor), Options);
		}
		return ParseTokensNDelimNChar(View, Delimiters, MoveTemp(Visitor), Options);
	default:
		if (Algo::AllOf(Delimiters, [](const FStringView& Delimiter) { return Delimiter.Len() == 1; }))
		{
			TArray<TCHAR, TInlineAllocator<32>> DelimiterChars;
			DelimiterChars.Reserve(Delimiters.Num());
			for (const FStringView& Delimiter : Delimiters)
			{
				DelimiterChars.Add(Delimiter[0]);
			}
			return ParseTokensNDelim1Char(View, DelimiterChars, MoveTemp(Visitor), Options);
		}
		else
		{
			return ParseTokensNDelimNChar(View, Delimiters, MoveTemp(Visitor), Options);
		}
	}
}

void ParseTokensMultiple(
	const FStringView View,
	const TConstArrayView<TCHAR> Delimiters,
	TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	switch (Delimiters.Num())
	{
	case 0:
		return ParseTokensVisitToken(Visitor, Options, View);
	case 1:
		return ParseTokens1Delim1Char(View, Delimiters[0], MoveTemp(Visitor), Options);
	default:
		return ParseTokensNDelim1Char(View, Delimiters, MoveTemp(Visitor), Options);
	}
}

void ParseTokens(
	const FStringView View,
	const FStringView Delimiter,
	TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	if (Delimiter.Len() == 1)
	{
		return ParseTokens1Delim1Char(View, Delimiter[0], MoveTemp(Visitor), Options);
	}
	return ParseTokensNDelimNChar(View, MakeArrayView(&Delimiter, 1), MoveTemp(Visitor), Options);
}

void ParseTokens(
	const FStringView View,
	const TCHAR Delimiter,
	TFunctionRef<void (FStringView)> Visitor,
	const EParseTokensOptions Options)
{
	return ParseTokens1Delim1Char(View, Delimiter, MoveTemp(Visitor), Options);
}

} // UE::String
