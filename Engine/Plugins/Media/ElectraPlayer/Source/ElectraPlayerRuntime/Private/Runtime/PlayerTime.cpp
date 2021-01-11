// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerTime.h"

#include "Math/BigInt.h"
#include "Utilities/StringHelpers.h"


namespace Electra
{

	//-----------------------------------------------------------------------------
	/**
	 * Returns this time value in a custom timebase. Requires internal bigint conversion and is therefor SLOW!
	 *
	 * @param CustomTimebase
	 *
	 * @return
	 */
	int64 FTimeValue::GetAsTimebase(uint32 CustomTimebase) const
	{
		// Some shortcuts
		if (!bIsValid)
		{
			return 0;
		}
		else if (bIsInfinity)
		{
			return HNS >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL;
		}
		else if (HNS == 0)
		{
			return 0;
		}

		TBigInt<128> n(HNS);
		TBigInt<128> d(10000000);
		TBigInt<128> s(CustomTimebase);

		n *= s;
		n /= d;

		int64 r = n.ToInt();
		return r;
	}

	// ---------------------------------------------------------------------------------------------------------------------

	FTimeFraction& FTimeFraction::SetFromFloatString(const FString& InString)
	{
		// The string value must consist only of a sign, decimal digits and an optional period.
		static const FString kTextDigitsEtc(TEXT("0123456789.-+"));
		static const FString kTextZero(TEXT("0"));
		if (StringHelpers::FindFirstNotOf(InString, kTextDigitsEtc) == INDEX_NONE) 
		{
			Denominator = 1;
			int32 DotIndex;
			InString.FindChar(TCHAR('.'), DotIndex);
			if (DotIndex == INDEX_NONE)
			{
				LexFromString(Numerator, *InString);
				bIsValid = true;
			}
			else
			{
				LexFromString(Numerator, *(InString.Mid(0, DotIndex)));
				FString frc = InString.Mid(DotIndex + 1);
				// Remove all trailing zeros
				int32 last0 = StringHelpers::FindLastNotOf(frc, kTextZero);
				if (last0 != INDEX_NONE)
				{
					frc.MidInline(0, last0 + 1);
					for (int32 i = 0; i < frc.Len(); ++i)
					{
						Numerator = Numerator * 10 + (frc[i] - TCHAR('0'));
						Denominator *= 10;
					}
				}
				bIsValid = true;
			}
		}
		else
		{
			bIsValid = false;
		}
		return *this;
	}


	int64 FTimeFraction::GetAsTimebase(uint32 CustomTimebase) const
	{
		// Some shortcuts
		if (!bIsValid)
		{
			return 0;
		}
		else if (Numerator == 0)
		{
			return 0;
		}
		// Infinity?
		else if (Denominator == 0 || CustomTimebase == 0)
		{
			return Numerator >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL;
		}

		TBigInt<128> n(Numerator);
		TBigInt<128> d(Denominator);
		TBigInt<128> s(CustomTimebase);

		n *= s;
		n /= d;

		int64 r = n.ToInt();
		return r;
	}


}

