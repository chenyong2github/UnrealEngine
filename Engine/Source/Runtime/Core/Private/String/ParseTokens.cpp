// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "String/ParseTokens.h"

#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Containers/StringView.h"

namespace UEString
{
	template <typename DelimiterType>
	inline static void ParseTokensMultipleImpl(const FStringView& View, TArrayView<const DelimiterType> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		// This is a naive implementation that takes time proportional to InView.Len() * TotalDelimiterLen.
		// If this function becomes a bottleneck, it can be specialized separately for single-character delimiters
		// and multi-character delimiters, as well as for one and many delimiters. There are algorithms for each of
		// these four combinations of requirements that are linear or sub-linear in the length of string to search.

		const FStringView::SizeType ViewLen = View.Len();
		FStringView::SizeType NextTokenIndex = 0;

		for (FStringView::SizeType ViewIndex = 0; ViewIndex != ViewLen;)
		{
			struct
			{
				FStringView RemainingView;
				inline bool operator()(const FStringView& InDelim) const { return RemainingView.StartsWith(InDelim, ESearchCase::CaseSensitive); }
				inline bool operator()(const TCHAR InDelim) const { return RemainingView.StartsWith(InDelim); }
				inline FStringView::SizeType Len(const FStringView& InDelim) const { return InDelim.Len(); }
				inline int32 Len(const TCHAR InDelim) const { return 1; }
			} MatchDelim{FStringView(View.GetData() + ViewIndex, ViewLen - ViewIndex)};

			if (const DelimiterType* Delim = Algo::FindByPredicate(Delimiters, MatchDelim))
			{
				Visitor(FStringView(View.GetData() + NextTokenIndex, ViewIndex - NextTokenIndex));
				ViewIndex += MatchDelim.Len(*Delim);
				NextTokenIndex = ViewIndex;
			}
			else
			{
				++ViewIndex;
			}
		}

		Visitor(FStringView(View.GetData() + NextTokenIndex, ViewLen - NextTokenIndex));
	}

	void ParseTokensMultiple(const FStringView& View, TArrayView<const FStringView> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		check(Algo::NoneOf(Delimiters, &FStringView::IsEmpty));
		ParseTokensMultipleImpl(View, Delimiters, Visitor);
	}

	void ParseTokensMultiple(const FStringView& View, TArrayView<const TCHAR> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		ParseTokensMultipleImpl(View, Delimiters, Visitor);
	}
}
