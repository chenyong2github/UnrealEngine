// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{
	/** Resolution of frequency value */
	enum class EFrequencyResolution : uint8
	{
		/** Frequency value given in hertz. */
		Hertz,

		/** Frequency value given in kilohertz. */
		Kilohertz,

		/** Frequency value given in megahertz. */
		Megahertz
	};

	/** FFrequency represents a frequency value. It provides clearly defined 
	 * getters and setters as well as several convenience functions for 
	 * converting frequencies to radians per sample..
	 */
	class FFrequency
	{
		public:
			/** Construct default frequency of 0 hz. */
			FFrequency()
			:	FrequencyInHz(0.f)
			{
			}

			/** Construct a frequency with a given value and resolution.
			 *
			 * @param InValue - The value of the frequency at the given 
			 * 					resolution.
			 * @param InResolution - The resolution of the given value.
			 */
			FFrequency(float InValue, EFrequencyResolution InResolution)
			:	FrequencyInHz(0.f)
			{
				switch (InResolution)
				{
					case EFrequencyResolution::Megahertz:
						SetMegahertz(InValue);
						break;

					case EFrequencyResolution::Kilohertz:
						SetKilohertz(InValue);
						break;

					case EFrequencyResolution::Hertz:
					default:
						SetHertz(InValue);
						break;
				}
			}

			/**
			 * FFrequency constructor used to pass in float literals from the metasound frontend.
			 */
			FFrequency(float InValue)
				: FFrequency(InValue, EFrequencyResolution::Hertz)
			{}

			/** Set the frequency in hertz. */
			void SetHertz(float InHz)
			{
				FrequencyInHz = InHz;	
			}

			/** Set the frequency in kilohertz. */
			void SetKilohertz(float InKHz)
			{
				FrequencyInHz = InKHz * 1e3f;
			}

			/** Set the frequency in megahertz. */
			void SetMegahertz(float InMHz)
			{
				FrequencyInHz = InMHz * 1e6f; 
			}

			/** Set the frequency in radians per a sample.
			 *
			 * @param InRadians - The number of radians per a sample.
			 * @param InSampleRate - The number of samples per a second.
			 */
			void SetRadiansPerSample(float InRadians, float InSampleRate)
			{
				FrequencyInHz = InSampleRate * InRadians / (2.f * PI);
			}

			/** Return the frequency in hertz. */
			float GetHertz() const
			{
				return FrequencyInHz;
			}

			/** Return the frequency in kilohertz. */
			float GetKilohertz() const
			{
				return FrequencyInHz * 1e-3f;
			}

			/** Return the frequency in megahertz. */
			float GetMegahertz() const
			{
				return FrequencyInHz * 1e-6f;
			}

			/** Return the radians per a sample.
			 *
			 * @param InSampleRate - The number of samples per a second.
			 *
			 * @return The phase increment as radians per a sample.
			 */
			float GetRadiansPerSample(float InSampleRate) const
			{
				if (!ensure(InSampleRate > 0.f))
				{
					InSampleRate = SMALL_NUMBER;
				}

				return (FrequencyInHz * 2.f * PI) / InSampleRate;
			}

			/** Return the number of radians for a given number of samples.
			 *
			 * @param InNumSamples - The number of samples.
			 * @param InSampleRate - The number of samples per a second.
			 *
			 * @return The number of radians for the given samples.
			 */
			float GetRadians(float InNumSamples, float InSampleRate) const
			{
				if (!ensure(InSampleRate > 0.f))
				{
					InSampleRate = SMALL_NUMBER;
				}

				return (InNumSamples * FrequencyInHz * 2.f * PI) / InSampleRate;
			}
		  
		private:

			float FrequencyInHz;
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FFrequency, METASOUNDSTANDARDNODES_API, FFrequencyTypeInfo, FFrequencyReadRef, FFrequencyWriteRef)
}
