// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "Templates/IsFloatingPoint.h"
#include "Templates/IsIntegral.h"
#include "Templates/IsSigned.h"

// Macros which can be enabled to cause DSP sample checking
#if 0
#define CHECK_SAMPLE(VALUE) 
#define CHECK_SAMPLE2(VALUE)
#else
#define CHECK_SAMPLE(VALUE)  Audio::CheckSample(VALUE)
#define CHECK_SAMPLE2(VALUE)  Audio::CheckSample(VALUE)
#endif

namespace Audio
{
	// Utility to check for sample clipping. Put breakpoint in conditional to find 
	// DSP code that's not behaving correctly
	static void CheckSample(float InSample, float Threshold = 0.001f)
	{	
		if (InSample > Threshold || InSample < -Threshold)
		{
			UE_LOG(LogTemp, Log, TEXT("SampleValue Was %.2f"), InSample);
		}
	}

	// Clamps floats to 0 if they are in sub-normal range
	static FORCEINLINE float UnderflowClamp(const float InValue)
	{
		if (InValue > -FLT_MIN && InValue < FLT_MIN)
		{
			return 0.0f;
		}
		return InValue;
	}

	// Function converts linear scale volume to decibels
	static FORCEINLINE float ConvertToDecibels(const float InLinear, const float InFloor = SMALL_NUMBER)
	{
		return 20.0f * FMath::LogX(10.0f, FMath::Max(InLinear, InFloor));
	}

	// Function converts decibel to linear scale
	static FORCEINLINE float ConvertToLinear(const float InDecibels)
	{
		return FMath::Pow(10.0f, InDecibels / 20.0f);
	}

	// Given a velocity value [0,127], return the linear gain
	static FORCEINLINE float GetGainFromVelocity(const float InVelocity)
	{
		if (InVelocity == 0.0f)
		{
			return 0.0f;
		}
		return (InVelocity * InVelocity) / (127.0f * 127.0f);
	}

	// Low precision, high performance approximation of sine using parabolic polynomial approx
	// Valid on interval [-PI, PI]
	static FORCEINLINE float FastSin(const float X)
	{
		return (4.0f * X) / PI * (1.0f - FMath::Abs(X) / PI);
	}

	// Slightly higher precision, high performance approximation of sine using parabolic polynomial approx
	static FORCEINLINE float FastSin2(const float X)
	{
		float X2 = FastSin(X);
		X2 = 0.225f * (X2* FMath::Abs(X2) - X2) + X2;
		return X2;
	}

	// Sine approximation using Bhaskara I technique discovered in 7th century. 
	// https://en.wikipedia.org/wiki/Bh%C4%81skara_I
	static FORCEINLINE float FastSin3(const float X)
	{
		// Component used to get negative radians
		const float SafeX = X < 0.0f ? FMath::Min(X, -SMALL_NUMBER) : FMath::Max(X, SMALL_NUMBER);
		const float Temp = SafeX * SafeX / FMath::Abs(SafeX);
		const float Numerator = 16.0f * SafeX * (PI - Temp);
		const float Denominator = 5.0f * PI * PI - 4.0f * Temp * (PI - Temp);
		return Numerator / Denominator;
	}

	// Fast tanh based on pade approximation
	static FORCEINLINE float FastTanh(float X)
	{
		if (X < -3) return -1.0f;
		if (X > 3) return 1.0f;
		const float InputSquared = X*X;
		return X*(27.0f + InputSquared) / (27.0f + 9.0f * InputSquared);
	}

	// Based on sin parabolic approximation
	static FORCEINLINE float FastTan(float X)
	{
		const float Num = X * (1.0f - FMath::Abs(X) / PI);
		const float Den = (X + 0.5f * PI) * (1.0f - FMath::Abs(X + 0.5f * PI) / PI);
		return Num / Den;
	}

	// Gets polar value from unipolar
	static FORCEINLINE float GetBipolar(const float X)
	{
		return 2.0f * X - 1.0f;
	}

	// Converts bipolar value to unipolar
	static FORCEINLINE float GetUnipolar(const float X)
	{
		return 0.5f * X + 0.5f;
	}

	// Using midi tuning standard, compute frequency in hz from midi value
	static FORCEINLINE float GetFrequencyFromMidi(const float InMidiNote)
	{
		return 440.0f * FMath::Pow(2.0f, (InMidiNote - 69.0f) / 12.0f);
	}

	// Returns the log frequency of the input value. Maps linear domain and range values to log output (good for linear slider controlling frequency)
	static FORCEINLINE float GetLogFrequencyClamped(const float InValue, const FVector2D& Domain, const FVector2D& Range)
	{
		const float InValueCopy = FMath::Clamp<float>(InValue, Domain.X, Domain.Y);
		const FVector2D RangeLog(FMath::Loge(Range.X), FMath::Loge(Range.Y));

		check(Domain.Y != Domain.X);
		const float Scale = (RangeLog.Y - RangeLog.X) / (Domain.Y - Domain.X);

		return FMath::Exp(RangeLog.X + Scale * (InValueCopy - Domain.X));
	}

	// Using midi tuning standard, compute midi from frequency in hz
	static FORCEINLINE float GetMidiFromFrequency(const float InFrequency)
	{
		return 69.0f + 12.0f * FMath::LogX(2.0f, InFrequency / 440.0f);
	}

	// Return a pitch scale factor based on the difference between a base midi note and a target midi note. Useful for samplers.
	static FORCEINLINE float GetPitchScaleFromMIDINote(int32 BaseMidiNote, int32 TargetMidiNote)
	{
		const float BaseFrequency = GetFrequencyFromMidi(FMath::Clamp((float)BaseMidiNote, 0.0f, 127.0f));
		const float TargetFrequency = 440.0f * FMath::Pow(2.0f, ((float)TargetMidiNote - 69.0f) / 12.0f);
		const float PitchScale = TargetFrequency / BaseFrequency;
		return PitchScale;
	}

	// Returns the frequency multipler to scale a base frequency given the input semitones
	static FORCEINLINE float GetFrequencyMultiplier(const float InPitchSemitones)
	{
		if (InPitchSemitones == 0.0f)
		{
			return 1.0f;

		}
		return FMath::Pow(2.0f, InPitchSemitones / 12.0f);
	}

	// Calculates equal power stereo pan using sinusoidal-panning law and cheap approximation for sin
	// InLinear pan is [-1.0, 1.0] so it can be modulated by a bipolar LFO
	static FORCEINLINE void GetStereoPan(const float InLinearPan, float& OutLeft, float& OutRight)
	{
		const float LeftPhase = 0.5f * PI * (0.5f * (InLinearPan + 1.0f) + 1.0f);
		const float RightPhase = 0.25f * PI * (InLinearPan + 1.0f);
		OutLeft = FMath::Clamp(FastSin(LeftPhase), 0.0f, 1.0f);
		OutRight = FMath::Clamp(FastSin(RightPhase), 0.0f, 1.0f);
	}
 
	// This function encodes a stereo Left/Right signal into a stereo Mid/Side signal 
	static FORCEINLINE void EncodeMidSide(float& LeftChannel, float& RightChannel)
	{
		const float Temp = (LeftChannel - RightChannel);
		//Output
		LeftChannel = (LeftChannel + RightChannel);
		RightChannel = Temp;
	}

	// This function decodes a stereo Mid/Side signal into a stereo Left/Right signal
	static FORCEINLINE void DecodeMidSide(float& MidChannel, float& SideChannel)
	{
		const float Temp = (MidChannel - SideChannel) * 0.5f;
		//Output
		MidChannel = (MidChannel + SideChannel) * 0.5f;
		SideChannel = Temp;
	}

	// Helper function to get bandwidth from Q
	static FORCEINLINE float GetBandwidthFromQ(const float InQ)
	{
		// make sure Q is not 0.0f, clamp to slightly positive
		const float Q = FMath::Max(KINDA_SMALL_NUMBER, InQ);
		const float Arg = 0.5f * ((1.0f / Q) + FMath::Sqrt(1.0f / (Q*Q) + 4.0f));
		const float OutBandwidth = 2.0f * FMath::LogX(2.0f, Arg);
		return OutBandwidth;
	}

	// Helper function get Q from bandwidth
	static FORCEINLINE float GetQFromBandwidth(const float InBandwidth)
	{
		const float InBandwidthClamped = FMath::Max(KINDA_SMALL_NUMBER, InBandwidth);
		const float Temp = FMath::Pow(2.0f, InBandwidthClamped);
		const float OutQ = FMath::Sqrt(Temp) / (Temp - 1.0f);
		return OutQ;
	}

	// Polynomial interpolation using lagrange polynomials. 
	// https://en.wikipedia.org/wiki/Lagrange_polynomial
	static FORCEINLINE float LagrangianInterpolation(const TArray<FVector2D> Points, const float Alpha)
	{
		float Lagrangian = 1.0f;
		float Output = 0.0f;

		const int32 NumPoints = Points.Num();
		for (int32 i = 0; i < NumPoints; ++i)
		{
			Lagrangian = 1.0f;
			for (int32 j = 0; j < NumPoints; ++j)
			{
				if (i != j)
				{
					float Denom = Points[i].X - Points[j].X;
					if (FMath::Abs(Denom) < SMALL_NUMBER)
					{
						Denom = SMALL_NUMBER;
					}
					Lagrangian *= (Alpha - Points[j].X) / Denom;
				}
			}
			Output += Lagrangian * Points[i].Y;
		}
		return Output;
	}

	// Simple exponential easing class. Useful for cheaply and smoothly interpolating parameters.
	class FExponentialEase
	{
	public:
		FExponentialEase(float InInitValue = 0.0f, float InEaseFactor = 0.001f, float InThreshold = KINDA_SMALL_NUMBER)
			: CurrentValue(InInitValue)
			, Threshold(InThreshold)
			, TargetValue(InInitValue)
			, EaseFactor(InEaseFactor)
			, OneMinusEase(1.0f - InEaseFactor)
			, EaseTimesTarget(EaseFactor * InInitValue)
		{
		}

		void Init(float InInitValue, float InEaseFactor = 0.001f)
		{
			CurrentValue = InInitValue;
			TargetValue = InInitValue;
			EaseFactor = InEaseFactor;

			OneMinusEase = 1.0f - EaseFactor;
			EaseTimesTarget = TargetValue * EaseFactor;
		}

		bool IsDone() const
		{
			return FMath::Abs(TargetValue - CurrentValue) < Threshold;
		}

		float GetNextValue()
		{
			if (IsDone())
			{
				return CurrentValue;
			}

			// Micro-optimization,
			// But since GetNextValue(NumTicksToJumpAhead) does this work in a tight loop (non-vectorizable), might as well
			/*
			return CurrentValue = CurrentValue + (TargetValue - CurrentValue) * EaseFactor;
								= CurrentValue + EaseFactor*TargetValue - EaseFactor*CurrentValue
								= (CurrentValue - EaseFactor*CurrentValue) + EaseFactor*TargetValue
								= (1 - EaseFactor)*CurrentValue + EaseFactor*TargetValue
			*/
			return CurrentValue = OneMinusEase * CurrentValue + EaseTimesTarget;
		}

		// same as GetValue(), but overloaded to jump forward by NumTicksToJumpAhead timesteps
		// (before getting the value)
		float GetNextValue(uint32 NumTicksToJumpAhead)
		{
			while (NumTicksToJumpAhead && !IsDone())
			{
				CurrentValue = OneMinusEase * CurrentValue + EaseTimesTarget;
				--NumTicksToJumpAhead;
			}

			return CurrentValue;
		}

		float PeekCurrentValue() const
		{
			return CurrentValue;
		}

		void SetEaseFactor(const float InEaseFactor)
		{
			EaseFactor = InEaseFactor;
			OneMinusEase = 1.0f - EaseFactor;
		}

		void operator=(const float& InValue)
		{
			SetValue(InValue);
		}

		void SetValue(const float InValue, const bool bIsInit = false)
		{
			TargetValue = InValue;
			EaseTimesTarget = EaseFactor * TargetValue;
			if (bIsInit)
			{
				CurrentValue = TargetValue;
			}
		}

		// This is a method for getting the factor to use for a given tau and sample rate.
		// Tau here is defined as the time it takes the interpolator to be within 1/e of it's destination.
		static float GetFactorForTau(float InTau, float InSampleRate)
		{
			return 1.0f - FMath::Exp(-1.0f / (InTau * InSampleRate));
		}

	private:

		// Current value of the exponential ease
		float CurrentValue;

		// Threshold to use to evaluate if the ease is done
		float Threshold;

		// Target value
		float TargetValue;

		// Percentage to move toward target value from current value each tick
		float EaseFactor;

		// 1.0f - EaseFactor
		float OneMinusEase;

		// EaseFactor * TargetValue
		float EaseTimesTarget;
	};
	
	// Simple easing function used to help interpolate params
	class FLinearEase 
	{
	public:
		FLinearEase()
			: StartValue(0.0f)
			, CurrentValue(0.0f)
			, DeltaValue(0.0f)
			, SampleRate(44100.0f)
			, DurationTicks(0)
			, DefaultDurationTicks(0)
			, CurrentTick(0)
			, bIsInit(true)
		{
		}

		~FLinearEase()
		{
		}

		bool IsDone() const
		{
			return CurrentTick >= DurationTicks;
		}

		void Init(float InSampleRate)
		{
			SampleRate = InSampleRate;
			bIsInit = true;
		}

		void SetValueRange(const float Start, const float End, const float InTimeSec)
		{
			StartValue = Start;
			CurrentValue = Start;
			SetValue(End, InTimeSec);
		}

		float GetNextValue()
		{
			if (IsDone())
			{
				return CurrentValue;
			}

			CurrentValue = DeltaValue * (float) CurrentTick / DurationTicks + StartValue;

			++CurrentTick;
			return CurrentValue;
		}

		// same as GetValue(), but overloaded to increment Current Tick by NumTicksToJumpAhead
		// (before getting the value)
		float GetNextValue(int32 NumTicksToJumpAhead)
		{
			if (IsDone())
			{
				return CurrentValue;
			}

			CurrentTick = FMath::Min(CurrentTick + NumTicksToJumpAhead, DurationTicks);
			CurrentValue = DeltaValue * (float)CurrentTick / DurationTicks + StartValue;

			return CurrentValue;
		}

		float PeekCurrentValue() const
		{
			return CurrentValue;
		}
		 
		// Updates the target value without changing the duration or tick data.
		// Sets the state as if the new value was the target value all along
		void SetValueInterrupt(const float InValue)
		{
			if (IsDone())
			{
				CurrentValue = InValue;
			}
			else
			{
				DurationTicks = DurationTicks - CurrentTick;
				CurrentTick = 0;
				DeltaValue = InValue - CurrentValue;
				StartValue = CurrentValue;
			}
		}

		void SetValue(const float InValue, float InTimeSec = 0.0f)
		{
			if (bIsInit)
			{
				bIsInit = false;
				DurationTicks = 0;
			}
			else
			{
				DurationTicks = (int32)(SampleRate * InTimeSec);
			}
			CurrentTick = 0;

			if (DurationTicks == 0)
			{
				CurrentValue = InValue;			
			}
			else
			{
				DeltaValue = InValue - CurrentValue;
				StartValue = CurrentValue;
			}
		}

	private:
		float StartValue;
		float CurrentValue;
		float DeltaValue;
		float SampleRate;
		int32 DurationTicks;
		int32 DefaultDurationTicks;
		int32 CurrentTick;
		bool bIsInit;
	};

	// Simple parameter object which uses critical section to write to and read from data
	template<typename T>
	class TParams
	{
	public:
		TParams()
			: bChanged(false)
		{}

		// Sets the params
		void SetParams(const T& InParams)
		{
			FScopeLock Lock(&CritSect);
			bChanged = true;
			CurrentParams = InParams;
		}

		// Returns a copy of the params safely if they've changed since last time this was called
		bool GetParams(T* OutParamsCopy)
		{
			FScopeLock Lock(&CritSect);
			if (bChanged)
			{
				bChanged = false;
				*OutParamsCopy = CurrentParams;
				return true;
			}
			return false;
		}

		bool bChanged;
		T CurrentParams;
		FCriticalSection CritSect;
	};

	/**
	 * Basic implementation of a circular buffer built for pushing and popping arbitrary amounts of data at once.
	 * Designed to be thread safe for SPSC; However, if Push() and Pop() are both trying to access an overlapping area of the buffer,
	 * One of the calls will be truncated. Thus, it is advised that you use a high enough capacity that the producer and consumer are never in contention.
	 */
	template <typename SampleType>
	class TCircularAudioBuffer
	{
	private:

		TArray<SampleType> InternalBuffer;
		uint32 Capacity;
		FThreadSafeCounter ReadCounter;
		FThreadSafeCounter WriteCounter;

	public:
		TCircularAudioBuffer()
		{
			SetCapacity(0);
		}

		TCircularAudioBuffer(uint32 InCapacity)
		{
			SetCapacity(InCapacity);
		}

		void SetCapacity(uint32 InCapacity)
		{
			checkf(InCapacity < (uint32)TNumericLimits<int32>::Max(), TEXT("Max capacity for this buffer is 2,147,483,647 samples. Otherwise our index arithmetic will not work."));
			Capacity = InCapacity + 1;
			ReadCounter.Set(0);
			WriteCounter.Set(0);
			InternalBuffer.Reset();
			InternalBuffer.AddZeroed(Capacity);
		}

		// Pushes some amount of samples into this circular buffer.
		// Returns the amount of samples written.
		int32 Push(const SampleType* InBuffer, uint32 NumSamples)
		{
			SampleType* DestBuffer = InternalBuffer.GetData();
			const uint32 ReadIndex = ReadCounter.GetValue();
			const uint32 WriteIndex = WriteCounter.GetValue();

			int32 NumToCopy = FMath::Min<int32>(NumSamples, Remainder());
			const int32 NumToWrite = FMath::Min<int32>(NumToCopy, Capacity - WriteIndex);
			FMemory::Memcpy(&DestBuffer[WriteIndex], InBuffer, NumToWrite * sizeof(SampleType));
					
			FMemory::Memcpy(&DestBuffer[0], &InBuffer[NumToWrite], (NumToCopy - NumToWrite) * sizeof(SampleType));

			WriteCounter.Set((WriteIndex + NumToCopy) % Capacity);

			return NumToCopy;
		}

		// Same as Pop(), but does not increment the read counter.
		int32 Peek(SampleType* OutBuffer, uint32 NumSamples)
		{
			SampleType* SrcBuffer = InternalBuffer.GetData();
			const uint32 ReadIndex = ReadCounter.GetValue();
			const uint32 WriteIndex = WriteCounter.GetValue();

			int32 NumToCopy = FMath::Min<int32>(NumSamples, Num());

			const int32 NumRead = FMath::Min<int32>(NumToCopy, Capacity - ReadIndex);
			FMemory::Memcpy(OutBuffer, &SrcBuffer[ReadIndex], NumRead * sizeof(SampleType));
				
			FMemory::Memcpy(&OutBuffer[NumRead], &SrcBuffer[0], (NumToCopy - NumRead) * sizeof(SampleType));

			check(NumSamples < ((uint32)TNumericLimits<int32>::Max()));

			return NumToCopy;
		}

		// Pops some amount of samples into this circular buffer.
		// Returns the amount of samples read.
		int32 Pop(SampleType* OutBuffer, uint32 NumSamples)
		{
			int32 NumSamplesRead = Peek(OutBuffer, NumSamples);
			check(NumSamples < ((uint32)TNumericLimits<int32>::Max()));

			ReadCounter.Set((ReadCounter.GetValue() + NumSamplesRead) % Capacity);

			return NumSamplesRead;
		}

		// When called, seeks the read or write cursor to only retain either the NumSamples latest data
		// (if bRetainOldestSamples is false) or the NumSamples oldest data (if bRetainOldestSamples is true)
		// in the buffer. Cannot be used to increase the capacity of this buffer.
		void SetNum(uint32 NumSamples, bool bRetainOldestSamples = false)
		{
			check(NumSamples < Capacity);

			if (bRetainOldestSamples)
			{
				WriteCounter.Set((ReadCounter.GetValue() + NumSamples) % Capacity);
			}
			else
			{
				int64 ReadCounterNum = ((int32)WriteCounter.GetValue()) - ((int32) NumSamples);
				if (ReadCounterNum < 0)
				{
					ReadCounterNum = Capacity + ReadCounterNum;
				}

				ReadCounter.Set(ReadCounterNum);
			}
		}

		// Get number of samples that can be popped off of the buffer.
		uint32 Num()
		{
			const int32 ReadIndex = ReadCounter.GetValue();
			const int32 WriteIndex = WriteCounter.GetValue();

			if (WriteIndex >= ReadIndex)
			{
				return WriteIndex - ReadIndex;
			}
			else
			{
				return Capacity - ReadIndex + WriteIndex;
			}
		}

		// Get the current capacity of the buffer
		uint32 GetCapacity()
		{
			return Capacity;
		}

		// Get number of samples that can be pushed onto the buffer before it is full.
		uint32 Remainder()
		{
			const uint32 ReadIndex = ReadCounter.GetValue();
			const uint32 WriteIndex = WriteCounter.GetValue();

			return (Capacity - 1 - WriteIndex + ReadIndex) % Capacity;
		}
	};

	/**
	 * This allows us to write a compile time exponent of a number.
	 */
	template <int Base, int Exp>
	struct TGetPower
	{
		static_assert(Exp >= 0, "TGetPower only supports positive exponents.");
		static const int64 Value = Base * TGetPower<Base, Exp - 1>::Value;
	};

	template <int Base>
	struct TGetPower<Base, 0>
	{
		static const int64 Value = 1;
	};

	/**
	 * TSample<SampleType, Q>
	 * Variant type to simplify converting and performing operations on fixed precision and floating point samples.
	 */
	template <typename SampleType, uint32 Q = (sizeof(SampleType) * 8 - 1)>
	class TSample
	{
		SampleType Sample;

		template <typename SampleTypeToCheck>
		static void CheckValidityOfSampleType()
		{
			constexpr bool bIsTypeValid = !!(TIsFloatingPoint<SampleTypeToCheck>::Value || TIsIntegral<SampleTypeToCheck>::Value);
			static_assert(bIsTypeValid, "Invalid sample type! TSampleRef only supports float or integer values.");
		}

		template <typename SampleTypeToCheck, uint32 QToCheck>
		static void CheckValidityOfQ()
		{
			// If this is a fixed-precision value, our Q offset must be less than how many bits we have.
			constexpr bool bIsTypeValid = !!(TIsFloatingPoint<SampleTypeToCheck>::Value || ((sizeof(SampleTypeToCheck) * 8) > QToCheck));
			static_assert(bIsTypeValid, "Invalid value for Q! TSampleRef only supports float or int types. For int types, Q must be smaller than the number of bits in the int type.");
		}

		// This is the number used to convert from float to our fixed precision value.
		static constexpr float QFactor = TGetPower<2, Q>::Value - 1;

		// for fixed precision types, the max and min values that we can represent are calculated here:
		static constexpr float MaxValue = TGetPower<2, (sizeof(SampleType) * 8 - Q)>::Value;
		static constexpr float MinValue = !!TIsSigned<SampleType>::Value ? (-1.0f * MaxValue) : 0.0f;

	public:

		TSample(SampleType& InSample)
			: Sample(InSample)
		{
			CheckValidityOfQ<SampleType, Q>();
			CheckValidityOfSampleType<SampleType>();
		}

		template<typename ReturnType = float>
		ReturnType AsFloat() const
		{
			static_assert(TIsFloatingPoint<ReturnType>::Value, "Return type for AsFloat() must be a floating point type.");

			if (TIsFloatingPoint<SampleType>::Value)
			{
				return static_cast<ReturnType>(Sample);
			}
			else if (TIsIntegral<SampleType>::Value)
			{
				// Cast from fixed to float.
				return static_cast<ReturnType>(Sample) / QFactor;
			}
			else
			{
				checkNoEntry();
				return static_cast<ReturnType>(Sample);
			}
		}

		template<typename ReturnType, uint32 ReturnQ = (sizeof(SampleType) * 8 - 1)>
		ReturnType AsFixedPrecisionInt()
		{
			static_assert(TIsIntegral<ReturnType>::Value, "This function must be called with an integer type as ReturnType.");
			CheckValidityOfQ<ReturnType, ReturnQ>();

			if (TIsIntegral<SampleType>::Value)
			{
				if (Q > ReturnQ)
				{
					return Sample << (Q - ReturnQ);
				}
				else if (Q < ReturnQ)
				{
					return Sample >> (ReturnQ - Q);
				}
				else
				{
					return Sample;
				}
			}
			else if (TIsFloatingPoint<SampleType>::Value)
			{
				static constexpr float ReturnQFactor = TGetPower<2, ReturnQ>::Value - 1;
				return (ReturnType)(Sample * ReturnQFactor);
			}
		}

		template <typename OtherSampleType>
		TSample<SampleType, Q>& operator =(const OtherSampleType InSample)
		{
			CheckValidityOfSampleType<OtherSampleType>();

			if (TIsSame<SampleType, OtherSampleType>::Value)
			{
				Sample = InSample;
				return *this;
			}
			else if (TIsIntegral<OtherSampleType>::Value && TIsFloatingPoint<SampleType>::Value)
			{
				// Cast from Q15 to float.
				Sample = ((SampleType)InSample) / QFactor;
				return *this;
			}
			else if (TIsFloatingPoint<OtherSampleType>::Value && TIsIntegral<SampleType>::Value)
			{
				// cast from float to Q15.
				Sample = static_cast<SampleType>(InSample * QFactor);
				return *this;
			}
			else
			{
				checkNoEntry();
				return *this;
			}
		}

		template <typename OtherSampleType>
		friend TSample<SampleType, Q> operator *(const TSample<SampleType, Q>& LHS, const OtherSampleType& RHS)
		{
			CheckValidityOfSampleType<OtherSampleType>();

			// Float case:
			if (TIsFloatingPoint<SampleType>::Value)
			{
				if (TIsFloatingPoint<OtherSampleType>::Value)
				{
					// float * float.
					return LHS.Sample * RHS;
				}
				else if (TIsIntegral<OtherSampleType>::Value)
				{
					// float * Q.
					SampleType FloatRHS = ((SampleType)RHS) / QFactor;
					return LHS.Sample * FloatRHS;
				}
				else
				{
					checkNoEntry();
					return LHS.Sample;
				}

			}
			// Q Case
			else if (TIsIntegral<SampleType>::Value)
			{
				if (TIsFloatingPoint<OtherSampleType>::Value)
				{
					// fixed * float.
					OtherSampleType FloatLHS = ((OtherSampleType)LHS.Sample) / QFactor;
					OtherSampleType Result = FMath::Clamp(FloatLHS * RHS, MinValue, MaxValue);
					return static_cast<SampleType>(Result * QFactor);
				}
				else if (TIsIntegral<OtherSampleType>::Value)
				{
					// Q * Q.
					float FloatLHS = ((float)LHS.Sample) / QFactor;
					float FloatRHS = ((float)RHS) / QFactor;
					float Result = FMath::Clamp(FloatLHS * FloatRHS, MinValue, MaxValue);
					return static_cast<OtherSampleType>(Result * QFactor);
				}
				else
				{
					checkNoEntry();
					return LHS.Sample;
				}
			}
			else
			{
				checkNoEntry();
				return LHS.Sample;
			}
		}
	};


	/**
	 * TSampleRef<SampleType, Q>
	 * Ref version of TSample. Useful for converting between fixed and float precisions.
	 * Example usage:
	 * int16 FixedPrecisionSample;
	 * TSampleRef<int16, 15> SampleRef(FixedPrecisionSample);
	 * 
	 * // Set the sample value directly:
	 * SampleRef = 0.5f;
	 * 
	 * // Or multiply the the sample:
	 * SampleRef *= 0.5f;
	 *
	 * bool bThisCodeWorks = FixedPrecisionSample == TNumericLimits<int16>::Max() / 4;
	 */
	template <typename SampleType, uint32 Q = (sizeof(SampleType) * 8 - 1)>
	class TSampleRef
	{
		SampleType& Sample;

		template <typename SampleTypeToCheck>
		static void CheckValidityOfSampleType()
		{
			constexpr bool bIsTypeValid = !!(TIsFloatingPoint<SampleTypeToCheck>::Value || TIsIntegral<SampleTypeToCheck>::Value);
			static_assert(bIsTypeValid, "Invalid sample type! TSampleRef only supports float or integer values.");
		}

		template <typename SampleTypeToCheck, uint32 QToCheck>
		static void CheckValidityOfQ()
		{
			// If this is a fixed-precision value, our Q offset must be less than how many bits we have.
			constexpr bool bIsTypeValid = !!(TIsFloatingPoint<SampleTypeToCheck>::Value || (sizeof(SampleTypeToCheck) * 8) > QToCheck);
			static_assert(bIsTypeValid, "Invalid value for Q! TSampleRef only supports float or int types. For int types, Q must be smaller than the number of bits in the int type.");
		}

		// This is the number used to convert from float to our fixed precision value.
		static constexpr float QFactor = TGetPower<2, Q>::Value - 1;

		// for fixed precision types, the max and min values that we can represent are calculated here:
		static constexpr float MaxValue = TGetPower<2, (sizeof(SampleType) * 8 - Q)>::Value;
		static constexpr float MinValue = !!TIsSigned<SampleType>::Value ? (-1.0f * MaxValue) : 0.0f;

	public:

		TSampleRef(SampleType& InSample)
			: Sample(InSample)
		{
			CheckValidityOfQ<SampleType, Q>();
			CheckValidityOfSampleType<SampleType>();
		}

		template<typename ReturnType = float>
		ReturnType AsFloat() const
		{
			static_assert(TIsFloatingPoint<ReturnType>::Value, "Return type for AsFloat() must be a floating point type.");

			if (TIsFloatingPoint<SampleType>::Value)
			{
				return static_cast<ReturnType>(Sample);
			}
			else if (TIsIntegral<SampleType>::Value)
			{
				// Cast from fixed to float.
				return static_cast<ReturnType>(Sample) / QFactor;
			}
			else
			{
				checkNoEntry();
				return static_cast<ReturnType>(Sample);
			}
		}

		template<typename ReturnType, uint32 ReturnQ = (sizeof(SampleType) * 8 - 1)>
		ReturnType AsFixedPrecisionInt()
		{
			static_assert(TIsIntegral<ReturnType>::Value, "This function must be called with an integer type as ReturnType.");
			
			CheckValidityOfQ<ReturnType, ReturnQ>();

			if (TIsIntegral<SampleType>::Value)
			{
				if (Q > ReturnQ)
				{
					return Sample << (Q - ReturnQ);
				}
				else if (Q < ReturnQ)
				{
					return Sample >> (ReturnQ - Q);
				}
				else
				{
					return Sample;
				}
			}
			else if (TIsFloatingPoint<SampleType>::Value)
			{
				static constexpr SampleType ReturnQFactor = TGetPower<2, ReturnQ>::Value - 1;
				return (ReturnType) (Sample * ReturnQFactor);
			}
		}

		template <typename OtherSampleType>
		TSampleRef<SampleType, Q>& operator =(const OtherSampleType InSample)
		{
			CheckValidityOfSampleType<OtherSampleType>();

			if (TIsSame<SampleType, OtherSampleType>::Value)
			{
				Sample = InSample;
				return *this;
			}
			else if (TIsIntegral<OtherSampleType>::Value && TIsFloatingPoint<SampleType>::Value)
			{
				// Cast from fixed to float.
				Sample = ((SampleType)InSample) / QFactor;
				return *this;
			}
			else if (TIsFloatingPoint<OtherSampleType>::Value && TIsIntegral<SampleType>::Value)
			{
				// cast from float to fixed.
				Sample = (SampleType)(InSample * QFactor);
				return *this;
			}
			else
			{
				checkNoEntry();
				return *this;
			}
		}

		template <typename OtherSampleType>
		friend SampleType operator *(const TSampleRef<SampleType>& LHS, const OtherSampleType& RHS)
		{
			CheckValidityOfSampleType<OtherSampleType>();

			// Float case:
			if (TIsFloatingPoint<SampleType>::Value)
			{
				if (TIsFloatingPoint<OtherSampleType>::Value)
				{
					// float * float.
					return LHS.Sample * RHS;
				}
				else if (TIsIntegral<OtherSampleType>::Value)
				{
					// float * fixed.
					SampleType FloatRHS = ((SampleType)RHS) / QFactor;
					return LHS.Sample * FloatRHS;
				}
				else
				{
					checkNoEntry();
					return LHS.Sample;
				}

			}
			// Fixed Precision Case
			else if (TIsIntegral<SampleType>::Value)
			{
				if (TIsFloatingPoint<OtherSampleType>::Value)
				{
					// fixed * float.
					OtherSampleType FloatLHS = ((OtherSampleType)LHS.Sample) / QFactor;
					OtherSampleType Result = FMath::Clamp(FloatLHS * RHS, MinValue, MaxValue);
					return static_cast<SampleType>(Result * QFactor);
				}
				else if (TIsIntegral<OtherSampleType>::Value)
				{
					// fixed * fixed.
					float FloatLHS = ((float)LHS.Sample) / QFactor;
					float FloatRHS = ((float)RHS) / QFactor;
					float Result = FMath::Clamp(FloatLHS * FloatRHS, MinValue, MaxValue);
					
					return static_cast<SampleType>(Result * QFactor);
				}
				else
				{
					checkNoEntry();
					return LHS.Sample;
				}
			}
			else
			{
				checkNoEntry();
				return LHS.Sample;
			}
		}
	};
}

