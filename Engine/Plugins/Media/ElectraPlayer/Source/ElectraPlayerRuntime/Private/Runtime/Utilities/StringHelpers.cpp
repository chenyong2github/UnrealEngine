// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utilities/StringHelpers.h"

namespace Electra
{
	namespace StringHelpers
	{


		int32 FindFirstOf(const FString& InString, const FString& SplitAt, int32 FirstPos)
		{
			if (SplitAt.Len() == 1)
			{
				// Speical version for only one split character
				const TCHAR& FindMe = SplitAt[0];
				for (int32 i = FirstPos; i < InString.Len(); ++i)
				{
					if (InString[i] == FindMe)
					{
						return i;
					}
				}
			}
			else
			{
				for (int32 i = FirstPos; i < InString.Len(); ++i)
				{
					for (int32 j = 0; j < SplitAt.Len(); ++j)
					{
						if (InString[i] == SplitAt[j])
						{
							return i;
						}
					}
				}
			}
			return INDEX_NONE;
		}


		int32 FindFirstNotOf(const FString& InString, const FString& InNotOfChars, int32 FirstPos)
		{
			for (int32 i = FirstPos; i < InString.Len(); ++i)
			{
				bool bFoundCharFromNotOfChars = false;
				for (int32 j = 0; j < InNotOfChars.Len(); ++j)
				{
					if (InString[i] == InNotOfChars[j])
					{
						// We found a character from the "NOT of" list. Check next...
						bFoundCharFromNotOfChars = true;
						break;
					}
				}
				if (!bFoundCharFromNotOfChars)
				{
					// We did not find any of the characters. This is what we are looking for and return the index of the first "not of" character
					return i;
				}
			}
			return INDEX_NONE;
		}


		int32 FindLastNotOf(const FString& InString, const FString& InNotOfChars, int32 InStartPos)
		{
			InStartPos = FMath::Min(InStartPos, InString.Len() - 1);
			for (int32 i = InStartPos; i >= 0; --i)
			{
				bool bFoundCharFromNotOfChars = false;
				for (int32 j = 0; j < InNotOfChars.Len(); ++j)
				{
					if (InString[i] == InNotOfChars[j])
					{
						// We found a character from the "NOT of" list. Check next...
						bFoundCharFromNotOfChars = true;
						break;
					}
				}
				if (!bFoundCharFromNotOfChars)
				{
					// We did not find any of the characters. This is what we are looking for and return the index of the first "not of" character
					return i;
				}
			}
			return INDEX_NONE;
		}


		void SplitByDelimiter(TArray<FString>& OutSplits, const FString& InString, const FString& SplitAt)
		{
			if (InString.Len())
			{
				int32 FirstPos = 0;
				while (1)
				{
					int32 SplitPos = InString.Find(SplitAt, ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstPos);
					FString subs = InString.Mid(FirstPos, SplitPos == INDEX_NONE ? MAX_int32 : SplitPos - FirstPos);
					if (subs.Len())
					{
						OutSplits.Push(subs);
					}
					if (SplitPos == INDEX_NONE)
					{
						break;
					}
					FirstPos = SplitPos + SplitAt.Len();
				}
			}
		}



	} // namespace StringHelpers
} // namespace Electra


