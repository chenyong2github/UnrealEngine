// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerTime.h"

#include "Math/BigInt.h"
#include "Utilities/StringHelpers.h"


namespace Electra
{

	FTimeValue& FTimeValue::SetFromTimeFraction(const FTimeFraction& TimeFraction)
	{
		if (TimeFraction.IsValid())
		{
			if (TimeFraction.IsPositiveInfinity())
			{
				SetToPositiveInfinity();
			}
			else
			{
				HNS = TimeFraction.GetAsTimebase(10000000);
				bIsInfinity = false;
				bIsValid = true;
			}
		}
		else
		{
			SetToInvalid();
		}
		return *this;
	}

	FTimeValue& FTimeValue::SetFromND(int64 Numerator, uint32 Denominator)
	{
		if (Denominator != 0)
		{
			if (Denominator == 10000000)
			{
				HNS 		= Numerator;
				bIsValid	= true;
				bIsInfinity = false;
			}
			else if (Numerator >= -922337203685LL && Numerator <= 922337203685LL)
			{
				HNS 		= Numerator * 10000000 / Denominator;
				bIsValid	= true;
				bIsInfinity = false;
			}
			else
			{
				SetFromTimeFraction(FTimeFraction(Numerator, Denominator));
			}
		}
		else
		{
			HNS 		= Numerator>=0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL;
			bIsValid	= true;
			bIsInfinity = true;
		}
		return *this;
	}

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
					// Convert at most 7 fractional digits (giving us hundreds of nanoseconds (HNS))
					for(int32 i = 0; i < frc.Len() && i<7; ++i)
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
			static const FString kInf0(TEXT("INF"));
			static const FString kInf1(TEXT("+INF"));
			static const FString kInf2(TEXT("-INF"));
			if (InString.Equals(kInf0) || InString.Equals(kInf1))
			{
				Numerator = 1;
				Denominator = 0;
				bIsValid = true;
			}
			else if (InString.Equals(kInf2))
			{
				Numerator = -1;
				Denominator = 0;
				bIsValid = true;
			}
			else
			{
				bIsValid = false;
			}
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
		else if (CustomTimebase == Denominator)
		{
			return Numerator;
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

