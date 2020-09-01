// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{ 
	/** Resolution of time value */
	enum class ETimeResolution : uint8
	{
		/** Time value given in seconds. */
		Seconds,

		/** Time value given in milliseconds. */
		Milliseconds,

		/** Time value given in microseconds. */
		Microseconds
	};

	/** TTime represents a time value. It provides basic mathematical 
	 * operations, clearly defined getters and setters, and interoperability 
	 * with TTime objects utilizing different template arguments. 
	 *
	 * QuantizationType declares the type of object which stores time 
	 * information.
	 *
	 * TimeType declares the type of object which represent time values 
	 * returned from TTime's getters and accepted by TTime's setters.
	 */
	template<typename QuantizationType, typename TimeType>
	class TTime
	{
			static_assert(TIsFloatingPoint<TimeType>::Value, "TimeType must be floating point");

		public:
			using FQuantizationType = QuantizationType;
			using FTimeTYpe = TimeType;

			/** Default constructor */
			TTime()
			:	Value(0)
			{}
			
			/** Initialize with a time value and time resolution.
			 *
			 * @param InValue - The initial value of the time object for the 
			 * 					given resolution.
			 * @param InResolution - The Resolution of the given InValue.
			 */
			TTime(QuantizationType InValue, ETimeResolution InResolution)
			:	Value(0)
			{
				// Different setters called based upon resolution.
				switch (InResolution)
				{
					case ETimeResolution::Microseconds:
						SetMicroseconds(InValue);
						break;

					case ETimeResolution::Milliseconds:
						SetMilliseconds(InValue);
						break;

					case ETimeResolution::Seconds:
					default:
						SetSeconds(InValue);
				}
			}

			/** Constructor used by the metasound backend. */
			TTime(QuantizationType InSeconds)
				: TTime(InSeconds, ETimeResolution::Seconds)
			{}

			/** Returns the time as seconds. */
			TimeType GetSeconds() const 
			{ 
				return static_cast<TimeType>(Value);
			}

			/** Returns the time as milliseconds. */
			TimeType GetMilliseconds() const 
			{ 
				return static_cast<TimeType>(Value * QuantizationType(1e3));
			}

			/** Returns the time as microseconds. */
			TimeType GetMicroseconds() const 
			{ 
				return static_cast<TimeType>(Value * QuantizationType(1e6));
			}

			/** Returns the number of samples which represent the current time 
			 * value.
			 *
			 * @param InSampleRate - The sample rate to use when calculating
			 * 						 the number of samples. 
			 *
			 * @return The number of samples. 
			 */
			QuantizationType GetNumSamples(TimeType InSampleRate) const
			{
				return static_cast<QuantizationType>(Value * InSampleRate);
			}

			/** Set the time value in seconds. */
			void SetSeconds(TimeType InSeconds) 
			{ 
				Value = static_cast<QuantizationType>(InSeconds);
			}

			/** Set the time value in milliseconds. */
			void SetMilliseconds(TimeType InMilliseconds) 
			{ 
				Value = static_cast<QuantizationType>(InMilliseconds * TimeType(1e-3));
			}

			/** Set the time value in microseconds. */
			void SetMicroseconds(TimeType InMicroseconds) 
			{ 
				Value = static_cast<QuantizationType>(InMicroseconds * TimeType(1e-6));
			}

			/** Set the number of samples to represent. 
			 *
			 * @param InNumSamples - The number of samples for the TTime object 
			 * 						 to represent.
			 * @param InSampleRate - The sample rate to use when calculating
			 * 						 the number of samples. 
			 */
			void SetNumSamples(QuantizationType InNumSamples, TimeType InSampleRate)
			{ 
				SetSeconds(InNumSamples / FMath::Max(InSampleRate, static_cast<TimeType>(SMALL_NUMBER)));
			}

			/** Assignment operator. */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator=(const TTime<OtherQuantizationType, OtherTimeType>& InOther)
			{
				Value = static_cast<QuantizationType>(InOther.GetSeconds());
				return *this;
			}

			/** Addition assignment operator. 
			 * 
			 * Adds the Other's time to this time.
			 */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator+=(const TTime<OtherQuantizationType, OtherTimeType>& InOther)
			{
				Value += static_cast<QuantizationType>(InOther.GetSeconds());
				return *this;
			}

			/** Subtraction assignment operator. 
			 * 
			 * Subtracts the Other's time from this time.
			 */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator-=(const TTime<OtherQuantizationType, OtherTimeType>& InOther)
			{
				Value -= static_cast<QuantizationType>(InOther.GetSeconds());
				return *this;
			}

			/* Multiplication assignment operator. 
			 *
			 * Multiplies this time by the value. 
			 */
			template<typename OtherType>
			TTime& operator*=(OtherType InValue)
			{
				static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be multiplied with arithmetic types");

				Value *= InValue;
				return *this;
			}

			/* Division assignment operator. 
			 *
			 * Divides this time by the value. 
			 */
			template<typename OtherType>
			TTime& operator/=(OtherType InValue)
			{
				static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be divided with arithmetic types");

				Value /= InValue;
				return *this;
			}

			/* Comparison operator global friend functions */

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator<(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator>(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator<=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator>=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator==(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator!=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			/* Arithmetic operator global friend functions */

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator+(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator-(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator*(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS);

			template<typename RHSQuantizationType, typename RHSTimeType, typename OtherType>
			friend TTime<RHSQuantizationType, RHSTimeType> operator*(OtherType LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator/(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS);


		private:

			QuantizationType Value;
	};


	// holds on to a count of samples.
	struct FTimeSampleCounter
	{
		// defines the type for counting samples.
		typedef int32 FCountType;
		FCountType Num;
	};

	/** TTime sample counting template specialization holds time values which
	 * are quantized to sample boundaries. It provides basic mathematical 
	 * operations, clearly defined getters and setters, and interoperability 
	 * with generic TTime objects utilizing different template arguments. 
	 *
	 * This specialization differs distinctly by storing the number of samples
	 * rather than the number of seconds in the quantization type. Additionally,
	 * instances have an associated sample rate member which is used to convert
	 * between TimeType and the sample count.
	 *
	 * TimeType declares the type of object which represent time values 
	 * returned from TTime's getters.
	 */
	template<typename TimeType>
	class TTime<FTimeSampleCounter, TimeType>
	{
		public:
			static_assert(TIsFloatingPoint<TimeType>::Value, "TimeType must be floating point");

			// using types from namespace.
			using FCountType = FTimeSampleCounter::FCountType;
			
			/** Construct a sample count based TTime with a given time value, 
			 * time resolution, and sample rate.
			 *
			 * @param InValue - The initial time value.
			 * @param InResolution - The time resolution of InValue.
			 * @param InSampleRate - The sample rate associated with this time 
			 * 						 object.
			 */
			TTime(TimeType InValue, ETimeResolution InResolution, TimeType InSampleRate)
			{
				// Need to set sample rate before setting time or else the 
				// resulting sample count will be incorrect.
				SetSampleRate(InSampleRate);

				switch (InResolution)
				{
					case ETimeResolution::Microseconds:
						SetMicroseconds(InValue);
						break;

					case ETimeResolution::Milliseconds:
						SetMilliseconds(InValue);
						break;

					case ETimeResolution::Seconds:
					default:
						SetSeconds(InValue);
				}
			}

			/**
			 * Constructor used by the Metasound Frontend.
			 *
			 * @param InSeconds The number in time to initialize to.
			 * @param InSettings the operator settings to use.
			 */
			TTime(float InSeconds, const FOperatorSettings& InSettings)
				: TTime(InSeconds, ETimeResolution::Seconds, InSettings.GetSampleRate())
			{}

			/** Construct a sample count based TTime using a number of samples
			 * and a sample rate.
			 *
			 * @param InNumSamples - The initial number of samples.
			 * @param InSampleRate - The sample rate of this instance.
			 */
			TTime(FCountType InNumSamples, TimeType InSampleRate)
			{
				SetSampleRate(InSampleRate);

				SetNumSamples(InNumSamples);
			}

			/** Set the sample rate of this object without changing the number
			 * of samples. This will result in the object representing a 
			 * different amount of time. 
			 */
			void SetSampleRate(TimeType InSampleRate)
			{
				if (!ensure(InSampleRate > 0))
				{
					InSampleRate = SMALL_NUMBER;
				}

				SampleRate = InSampleRate;
			}

			/** Set the sample rate of this object and change the number
			 * of samples. This will result in the object representing 
			 * approximately the same amount of time as it did before the 
			 * sample rate was changed.
			 */
			void SetSampleRateAndUpdateSampleCount(TimeType InSampleRate)
			{
				if (!ensure(InSampleRate > 0))
				{
					InSampleRate = SMALL_NUMBER;
				}

				Value.Num = GetNumSamples(InSampleRate);
				
				SampleRate = InSampleRate;
			}

			/** Return the sample rate. */
			TimeType GetSampleRate() const
			{
				return SampleRate;
			}

			/** Return the time as seconds. */
			TimeType GetSeconds() const 
			{ 
				return static_cast<TimeType>(Value.Num / SampleRate);
			}

			/** Return the time as milliseconds. */
			TimeType GetMilliseconds() const 
			{ 
				return static_cast<TimeType>(Value.Num * 1e3 / SampleRate);
			}

			/** Return the time as microseconds. */
			TimeType GetMicroseconds() const 
			{ 
				return static_cast<TimeType>(Value.Num * 1e6 / SampleRate);
			}

			/** Return the number of samples. */
			FCountType GetNumSamples() const
			{ 
				return Value.Num;
			}

			/** Return the number of samples which represent this time duration
			 * using a different sample rate. 
			 *
			 * @param InOtherSampleRate - Sample rate to use when calculating 
			 * 							  the number of samples.  
			 */
			FCountType GetNumSamples(TimeType InOtherSampleRate) const
			{ 
				if (InOtherSampleRate == SampleRate)
				{
					return Value.Num;
				}
				else
				{
					return FMath::RoundToInt(GetSeconds() * InOtherSampleRate);
				}
			}

			/** Set the time value in seconds. */
			void SetSeconds(TimeType InSeconds) 
			{ 
				Value.Num = FMath::RoundToInt(InSeconds * SampleRate);
			}

			/** Set the time value in milliseconds. */
			void SetMilliseconds(TimeType InMilliseconds) 
			{ 
				Value.Num = FMath::RoundToInt(InMilliseconds * SampleRate * TimeType(1e-3));
			}

			/** Set the time value in microseconds. */
			void SetMicroseconds(TimeType InMicroseconds) 
			{ 
				Value.Num = FMath::RoundToInt(InMicroseconds * SampleRate * TimeType(1e-6));
			}

			/** Set the number of samples. */
			void SetNumSamples(FCountType InNumSamples)
			{ 
				Value.Num = InNumSamples;
			}

			/** Assignment operator 
			 *
			 * This assignment preserves this objects sample rate when being 
			 * assigned to TTime objects of different quantization types.
			 * */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator=(const TTime<OtherQuantizationType, OtherTimeType>& Other)
			{
				Value.Num = Other.GetNumSamples(SampleRate);

				return *this;
			}

			/** Assignment operator 
			 *
			 * This assignment copies the sample rate and sample count of TTime
			 * objects of equal quantization types.
			 */
			template<typename OtherTimeType>
			TTime& operator=(const TTime<FTimeSampleCounter, OtherTimeType>& Other)
			{
				Value.Num = Other.Value.Num;
				SampleRate = Other.SampleRate;

				return *this;
			}

			/** Addition assignment operator. */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator+=(const TTime<OtherQuantizationType, OtherTimeType>& Other)
			{
				Value.Num += Other.GetNumSamples(SampleRate);

				return *this;
			}

			/** Subtraction assignment operator. */
			template<typename OtherQuantizationType, typename OtherTimeType>
			TTime& operator-=(const TTime<OtherQuantizationType, OtherTimeType>& Other)
			{
				Value.Num -= Other.GetNumSamples(SampleRate);

				return *this;
			}

			/** Multiplication assignment operator. */
			template<typename OtherType>
			TTime& operator*=(OtherType Other)
			{
				static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be multiplied with arithmetic types");

				Value.Num *= Other;
				return *this;
			}

			/** Division assignment operator. */
			template<typename OtherType>
			TTime& operator/=(OtherType Other)
			{
				static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be divided with arithmetic types");

				Value.Num /= Other;
				return *this;
			}

			/* Comparison operator global friend functions */
			
			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator<(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator>(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator<=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator>=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator==(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend bool operator!=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			/* Arithmetic operator global friend functions */

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator+(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator-(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator*(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS);

			template<typename RHSQuantizationType, typename RHSTimeType, typename OtherType>
			friend TTime<RHSQuantizationType, RHSTimeType> operator*(OtherType LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS);

			template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
			friend TTime<LHSQuantizationType, LHSTimeType> operator/(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS);

		private:

			FTimeSampleCounter Value = {0};
			TimeType SampleRate = static_cast<TimeType>(SMALL_NUMBER);
	};


	/** Less-than comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator<(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{ 
		return LHS.GetSeconds() < RHS.GetSeconds();
	}

	/** Greater-than comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator>(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{ 
		return RHS < LHS; 
	}

	/** Less-than-or-equal-to comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator<=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{ 
		return !(LHS > RHS); 
	}

	/** Greater-than-or-equal-to comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator>=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{ 
		return !(LHS < RHS); 
	}

	/** Equal comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator==(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{
		return LHS.GetSeconds() == RHS.GetSeconds();
	}

	/** Not-equal comparison of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	bool operator!=(const TTime<LHSQuantizationType, LHSTimeType>& LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{ 
		return !(LHS == RHS); 
	}

	/** Addition operator of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	TTime<LHSQuantizationType, LHSTimeType> operator+(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{
		LHS += RHS;
		return LHS;
	}

	/** Subtraction operator of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename RHSQuantizationType, typename RHSTimeType>
	TTime<LHSQuantizationType, LHSTimeType> operator-(TTime<LHSQuantizationType, LHSTimeType> LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{
		LHS -= RHS;
		return LHS;
	}

	/** Multiplication operator of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
	TTime<LHSQuantizationType, LHSTimeType> operator*(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS)
	{
		static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be multiplied with arithmetic types");

		LHS *= RHS;
		return LHS;
	}

	/** Division operator of TTime */
	template<typename LHSQuantizationType, typename LHSTimeType, typename OtherType>
	TTime<LHSQuantizationType, LHSTimeType> operator/(TTime<LHSQuantizationType, LHSTimeType> LHS, OtherType RHS)
	{
		static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be divided with arithmetic types");

		LHS /= RHS;
		return LHS;
	}

	/** Multiplication operator of TTime */
	template<typename RHSQuantizationType, typename RHSTimeType, typename OtherType>
	TTime<RHSQuantizationType, RHSTimeType> operator*(OtherType LHS, const TTime<RHSQuantizationType, RHSTimeType>& RHS)
	{
		static_assert(TIsArithmetic<OtherType>::Value, "TTime can only be multiplied with arithmetic types");

		return RHS * LHS;
	}
	

	/** FFloatTime stores and represents all time values as 32bit floating 
	 * point values. 
	 */
	typedef TTime<float, float> FFloatTime;

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FFloatTime, METASOUNDSTANDARDNODES_API, FFloatTimeTypeInfo, FFloatTimeReadRef, FFloatTimeWriteRef);

	/** FDoubleTime stores and represents all time values as 64bit floating
	 * point values.
	 */
	typedef TTime<double, double> FDoubleTime;

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FDoubleTime, METASOUNDSTANDARDNODES_API, FDoubleTimeTypeInfo, FDoubleTimeReadRef, FDoubleTimeWriteRef);

	/** FSampleTime stores time quantized to sample boundaries. It returns time
	 * as 32bit floating point values.
	 */
	typedef TTime<FTimeSampleCounter, float> FSampleTime;

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FSampleTime, METASOUNDSTANDARDNODES_API, FSampleTimeTypeInfo, FSampleTimeReadRef, FSampleTimeWriteRef);
}
