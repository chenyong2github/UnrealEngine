// Copyright Epic Games, Inc. All Rights Reserved.

#include "UDIMUtilities.h"

#include "Internationalization/Regex.h"

namespace UE
{
	namespace TextureUtilitiesCommon
	{
		uint32 ParseUDIMName(const FString& Name, const FString& UdimRegexPattern, FString& OutPrefixName, FString& OutPostfixName)
		{
			FRegexPattern RegexPattern( UdimRegexPattern );
			FRegexMatcher RegexMatcher( RegexPattern, Name );

			int32 UdimValue = INDEX_NONE;

			if ( RegexMatcher.FindNext() )
			{
				const int32 StartOfCaptureGroup1 = RegexMatcher.GetCaptureGroupBeginning(1);
				const int32 EndOfCaptureGroup1 = RegexMatcher.GetCaptureGroupEnding(1);
				const int32 StartOfCaptureGroup2 = RegexMatcher.GetCaptureGroupBeginning(2);
				const int32 EndOfCaptureGroup2 = RegexMatcher.GetCaptureGroupEnding(2);
				const int32 StartOfCaptureGroup3 = RegexMatcher.GetCaptureGroupBeginning(3);
				const int32 EndOfCaptureGroup3 = RegexMatcher.GetCaptureGroupEnding(3);

				if ( StartOfCaptureGroup1 != INDEX_NONE && StartOfCaptureGroup2 != INDEX_NONE &&
					 EndOfCaptureGroup1 != INDEX_NONE && EndOfCaptureGroup2 != INDEX_NONE )
				{
					LexFromString( UdimValue, *Name.Mid( StartOfCaptureGroup2, EndOfCaptureGroup2 - StartOfCaptureGroup2 ) );

					OutPrefixName = Name.Mid( StartOfCaptureGroup1, EndOfCaptureGroup1 - StartOfCaptureGroup1 );

					if ( StartOfCaptureGroup3 != INDEX_NONE && EndOfCaptureGroup3 != INDEX_NONE )
					{
						OutPostfixName = Name.Mid( StartOfCaptureGroup3, EndOfCaptureGroup3 - StartOfCaptureGroup3 );
					}
				}
			}
	
			if ( UdimValue < 1001 )
			{
				// UDIM starts with 1001 as the origin
				return INDEX_NONE;
			}

			return UdimValue;
		}

		int32 GetUDIMIndex(int32 BlockX, int32 BlockY)
		{
			return BlockY * 10 + BlockX + 1001;
		}

		void ExtractUDIMCoordinates(int32 UDIMIndex, int32& OutBlockX, int32& OutBlockY)
		{
			OutBlockX = (UDIMIndex - 1001) % 10;
			OutBlockY = (UDIMIndex - 1001) / 10;
		}
	}
}
