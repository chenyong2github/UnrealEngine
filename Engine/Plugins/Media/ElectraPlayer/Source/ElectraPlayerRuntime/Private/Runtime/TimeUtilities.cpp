// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerTime.h"
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
				SetFromND(TimeFraction.GetNumerator(), TimeFraction.GetDenominator());
			}
		}
		else
		{
			SetToInvalid();
		}
		return *this;
	}



	namespace
	{
		struct FTimeComponents
		{
			FTimeComponents()
				: Years(0)
				, Months(0)
				, Days(0)
				, Hours(0)
				, Minutes(0)
				, Seconds(0)
				, Milliseconds(0)
				, TimezoneMinutes(0)
			{
			}
			int32		Years;
			int32		Months;
			int32		Days;
			int32		Hours;
			int32		Minutes;
			int32		Seconds;
			int32		Milliseconds;
			int32		TimezoneMinutes;

			bool IsValidUTC() const
			{
				return Years >= 1970 && Years < 2200 /* arbitrary choice in the future */ && Months >= 1 && Months <= 12 && Days >= 1 && Days <= 31 &&
					Hours >= 0 && Hours <= 23 && Minutes >= 0 && Minutes <= 59 && Seconds >= 0 && Seconds <= 60 /*allow for leap second*/ &&
					Milliseconds >= 0 && Milliseconds <= 999 &&
					TimezoneMinutes >= -14 * 60 && TimezoneMinutes <= 14 * 60 /* +/- 14 hours*/;
			}

			static bool IsDigit(const TCHAR c)
			{
				return c >= TCHAR('0') && c <= TCHAR('9');
			}

			static bool IsLeapYear(int32 InYear)
			{
				return (InYear % 4) == 0 && ((InYear % 100) != 0 || (InYear % 400) == 0);
			};

			FTimeValue ToUTC() const
			{
				static const uint8 DaysInMonths[2][12] = { {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31} };
				int64 Sum = 0;
				for (int32 y = 1970; y < Years; ++y)
				{
					Sum += IsLeapYear(y) ? 366 : 365;
				}
				const uint8* const DaysPerMonth = DaysInMonths[IsLeapYear(Years) ? 1 : 0];
				for (int32 m = 1; m < Months; ++m)
				{
					Sum += DaysPerMonth[m - 1];
				}
				Sum += Days - 1;
				Sum *= 24;
				Sum += Hours;
				Sum *= 60;
				Sum += Minutes;
				Sum *= 60;
				Sum += Seconds;
				Sum -= TimezoneMinutes * 60;
				Sum *= 1000;
				Sum += Milliseconds;
				return FTimeValue().SetFromMilliseconds(Sum);
			}
		};


		static int32 ParseSubStringToInt(TCHAR* InFrom, int32 NumChars)
		{
			TCHAR* last = InFrom + NumChars;
			TCHAR c = *last;
			*last = TCHAR('\0');
			int32 v;
			LexFromString(v, InFrom);
			*last = c;
			return v;
		}

	}






	namespace ISO8601
	{

		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime)
		{
			// Is this a valid date time string in extended format (we require colons and dashes)
			// 		YYYY-MM-DDTHH:MM:SS[.s*][Z]
			if (DateTime.Len() >= 19 && DateTime[10] == TCHAR('T') && DateTime[4] == TCHAR('-') && DateTime[7] == TCHAR('-') && DateTime[13] == TCHAR(':') && DateTime[16] == TCHAR(':'))
			{
				// Make a mutable copy of the string we can alter.
				FString tempString(DateTime);
				TCHAR* tempBuf = GetData(tempString);

				FTimeComponents TimeComponents;

				if (FTimeComponents::IsDigit(tempBuf[0]) && FTimeComponents::IsDigit(tempBuf[1]) && FTimeComponents::IsDigit(tempBuf[2]) && FTimeComponents::IsDigit(tempBuf[3]))
				{
					TimeComponents.Years = ParseSubStringToInt(tempBuf, 4);
					if (FTimeComponents::IsDigit(tempBuf[5]) && FTimeComponents::IsDigit(tempBuf[6]))
					{
						TimeComponents.Months = ParseSubStringToInt(tempBuf + 5, 2);
						if (FTimeComponents::IsDigit(tempBuf[8]) && FTimeComponents::IsDigit(tempBuf[9]))
						{
							TimeComponents.Days = ParseSubStringToInt(tempBuf + 8, 2);
							if (FTimeComponents::IsDigit(tempBuf[11]) && FTimeComponents::IsDigit(tempBuf[12]))
							{
								TimeComponents.Hours = ParseSubStringToInt(tempBuf + 11, 2);
								if (FTimeComponents::IsDigit(tempBuf[14]) && FTimeComponents::IsDigit(tempBuf[15]))
								{
									TimeComponents.Minutes = ParseSubStringToInt(tempBuf + 14, 2);
									if (FTimeComponents::IsDigit(tempBuf[17]) && FTimeComponents::IsDigit(tempBuf[18]))
									{
										TimeComponents.Seconds = ParseSubStringToInt(tempBuf + 17, 2);

										int32 milliSeconds = 0;
										// Are there fractions or timezone?
										TCHAR* Suffix = tempBuf + 19;
										if (Suffix[0] == TCHAR('.'))
										{
											uint32 secFractionalScale = 3;
											for (++Suffix; FTimeComponents::IsDigit(*Suffix); ++Suffix)
											{
												// Only consider 3 fractional digits.
												if (secFractionalScale)
												{
													--secFractionalScale;
													milliSeconds = milliSeconds * 10 + (*Suffix - TCHAR('0'));
												}
											}
											while (secFractionalScale)
											{
												--secFractionalScale;
												milliSeconds *= 10;
											}
										}
										TimeComponents.Milliseconds = milliSeconds;

										// Time zone suffix?
										if (Suffix[0] == TCHAR('+') || Suffix[0] == TCHAR('-'))
										{
											// Offsets have to be [+|-][hh[[:]mm]]
											int32 tzLen = FCString::Strlen(Suffix);
											TCHAR tzSign = Suffix[0];
											if (tzLen >= 3)
											{
												int32 offM = 0;
												int32 offH = ParseSubStringToInt(Suffix + 1, 2);
												Suffix += 3;
												tzLen -= 3;
												// There may or may not be a colon to separate minutes from hours, or no minutes at all.
												if (tzLen)
												{
													if (Suffix[0] == TCHAR(':'))
													{
														Suffix += 1;
														tzLen -= 1;
													}
													// Minutes?
													if (tzLen >= 2)
													{
														offM = ParseSubStringToInt(Suffix, 2);
														Suffix += 2;
													}
												}
												TimeComponents.TimezoneMinutes = offH * 60 + offM;
												if (tzSign == TCHAR('-'))
												{
													TimeComponents.TimezoneMinutes = -TimeComponents.TimezoneMinutes;
												}
											}
											else
											{
												return UEMEDIA_ERROR_FORMAT_ERROR;
											}
										}
										else if (Suffix[0] == TCHAR('Z'))
										{
											// We treat the time as UTC in all cases. If it's already that, fine.
											++Suffix;
										}
										if (Suffix[0] != TCHAR('\0'))
										{
											return UEMEDIA_ERROR_FORMAT_ERROR;
										}
									}
								}
							}
						}
					}
				}

				if (TimeComponents.IsValidUTC())
				{
					OutTimeValue = TimeComponents.ToUTC();
					return UEMEDIA_ERROR_OK;
				}
			}
			return UEMEDIA_ERROR_FORMAT_ERROR;
		}


	} // namespace ISO8601


	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/


	namespace RFC7231
	{

		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime)
		{
			static const TCHAR* const MonthNames[] = { TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec") };

			// Three formats need to be considered as per RFC 7231 section 7.1.1.1. :
			//   First the preferred format, if that is no match then obsolete RFC 850 and finally ANSI C asctime()
			// We split the string by space and comma to get 6 groups for IMF, 4 for RFC850 and 5 for asctime.
			TCHAR Groups[6][16];
			const TCHAR* s = GetData(DateTime);
			int32 NumGroups = 0;
			while (*s)
			{
				if (*s == TCHAR(' ') || *s == TCHAR(','))
				{
					++s;
				}
				else
				{
					TCHAR* Group = Groups[NumGroups];
					if (++NumGroups > 6)
					{
						return UEMEDIA_ERROR_FORMAT_ERROR;
					}
					int32 GroupLen = 0;
					while (*s && *s != TCHAR(' ') && *s != TCHAR(','))
					{
						*Group++ = *s++;
						// Check that we do not exceed our fixed size array (including one to add terminating NUL char)
						if (++GroupLen == sizeof(Groups[0]) - 1)
						{
							return UEMEDIA_ERROR_FORMAT_ERROR;
						}
					}
					// Terminate the group.
					*Group = TCHAR('\0');
				}
			}

			FTimeComponents TimeComponents;

			// Preferred format: IMF-fixdate, ie:  Sun, 06 Nov 1994 08:49:37 GMT
			if (NumGroups == 6)
			{
				TimeComponents.Hours = ParseSubStringToInt(Groups[4], 2);
				TimeComponents.Minutes = ParseSubStringToInt(Groups[4] + 3, 2);
				TimeComponents.Seconds = ParseSubStringToInt(Groups[4] + 6, 2);
				LexFromString(TimeComponents.Days, Groups[1]);
				LexFromString(TimeComponents.Years, Groups[3]);
				for (int32 Month = 0; Month < FMEDIA_STATIC_ARRAY_COUNT(MonthNames); ++Month)
				{
					if (FCString::Strcmp(Groups[2], MonthNames[Month]) == 0)
					{
						TimeComponents.Months = Month + 1;
						break;
					}
				}
			}
			// Obsolete RFC 850 format, ie:  Sunday, 06-Nov-94 08:49:37 GMT
			else if (NumGroups == 4)
			{
				TimeComponents.Hours = ParseSubStringToInt(Groups[2], 2);
				TimeComponents.Minutes = ParseSubStringToInt(Groups[2] + 3, 2);
				TimeComponents.Seconds = ParseSubStringToInt(Groups[2] + 6, 2);

				if (FCString::Strlen(Groups[1]) != 9)
				{
					return UEMEDIA_ERROR_FORMAT_ERROR;
				}
				TimeComponents.Days = ParseSubStringToInt(Groups[1], 2);
				TimeComponents.Years = ParseSubStringToInt(Groups[1] + 7, 2);
				// 1970-2069
				TimeComponents.Years += TimeComponents.Years >= 70 ? 1900 : 2000;
				for (int32 Month = 0; Month < FMEDIA_STATIC_ARRAY_COUNT(MonthNames); ++Month)
				{
					if (FCString::Strncmp(Groups[1] + 3, MonthNames[Month], 3) == 0)
					{
						TimeComponents.Months = Month + 1;
						break;
					}
				}
			}
			// ANSI C asctime() format, ie: Sun Nov  6 08:49:37 1994
			else if (NumGroups == 5)
			{
				TimeComponents.Hours = ParseSubStringToInt(Groups[3], 2);
				TimeComponents.Minutes = ParseSubStringToInt(Groups[3] + 3, 2);
				TimeComponents.Seconds = ParseSubStringToInt(Groups[3] + 6, 2);
				LexFromString(TimeComponents.Days, Groups[2]);
				LexFromString(TimeComponents.Years, Groups[4]);
				for (int32 Month = 0; Month < FMEDIA_STATIC_ARRAY_COUNT(MonthNames); ++Month)
				{
					if (FCString::Strcmp(Groups[1], MonthNames[Month]) == 0)
					{
						TimeComponents.Months = Month + 1;
						break;
					}
				}
			}
			else
			{
				return UEMEDIA_ERROR_FORMAT_ERROR;
			}

			if (TimeComponents.IsValidUTC())
			{
				OutTimeValue = TimeComponents.ToUTC();
				return UEMEDIA_ERROR_OK;
			}
			return UEMEDIA_ERROR_FORMAT_ERROR;
		}

	} // namespace RFC7231



} // namespace Electra


