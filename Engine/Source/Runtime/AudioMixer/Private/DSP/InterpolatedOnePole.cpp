#include "DSP/InterpolatedOnePole.h"

namespace Audio
{

	// INTERPOLATED ONE-POLE LOW-PASS IMPLEMENTATION
	FInterpolatedLPF::FInterpolatedLPF()
	{
		Z1.Init(0.0f, NumChannels);
		Reset();
	}


	void FInterpolatedLPF::Init(float InSampleRate, int32 InNumChannels)
	{
		SampleRate = InSampleRate;
		NumChannels = InNumChannels;
		CutoffFrequency = -1.0f;
		Z1.Init(0.0f, NumChannels);
		Reset();
	}

	void FInterpolatedLPF::StartFrequencyInterpolation(const float InTargetFrequency, const int32 InterpLength)
	{
		CurrInterpLength = InterpLength;
		CurrInterpCounter = 0;

		if (!FMath::IsNearlyEqual(InTargetFrequency, CutoffFrequency))
		{
			CutoffFrequency = InTargetFrequency;
			float NormalizedFreq = FMath::Clamp(0.5f * InTargetFrequency / SampleRate, 0.0f, 1.0f);
			B1Target = FMath::Exp(-PI * NormalizedFreq);
			B1Delta = (B1Target - B1Curr) / static_cast<float>(InterpLength);
		}
	}

	void FInterpolatedLPF::ProcessAudioFrame(float* RESTRICT InputFrame, float* RESTRICT OutputFrame)
	{
		B1Curr += B1Delta; // step forward coefficient
		++CurrInterpCounter;

		/*
			[absorbing A0 coefficient]
			-----------------------------%
			Yn = Xn*A0 + B1*Z1;                <- old way
			A0 = (1-B1)

			Yn = Xn*(1-B1) + B1*Z1             <- (1 add, 1 sub, 2 mult)
			Yn = Xn - B1*Xn + B1*Z1
			Yn = Xn + B1*Z1 - B1*Xn
			Yn = Xn + B1*(Z1 - Xn)             <- (1 add, 1 sub, 1 mult)
		*/

		for (int32 i = 0; i < NumChannels; ++i)
		{
			float* Z1Data = Z1.GetData();
			const float InputSample = InputFrame[i];
			float Yn = InputSample + B1Curr * (Z1Data[i] - InputSample); // LPF
			Yn = UnderflowClamp(Yn);
			Z1Data[i] = Yn;
			OutputFrame[i] = Yn;
		}
	}

	void FInterpolatedLPF::Reset()
	{
		B1Curr = 0.0f;
		B1Delta = 0.0f;
		B1Target = B1Curr;
		CurrInterpLength = 0;
		CurrInterpCounter = 0;
		ClearMemory();
	}

	void FInterpolatedLPF::ClearMemory()
	{
		Z1.Reset();
		Z1.AddZeroed(NumChannels);
	}


	// INTERPOLATED ONE-POLE HIGH-PASS IMPLEMENTATION
	FInterpolatedHPF::FInterpolatedHPF()
	{
		Z1.Init(0.0f, NumChannels);
		Reset();
	}

	void FInterpolatedHPF::Init(float InSampleRate, int32 InNumChannels)
	{
		SampleRate = InSampleRate;
		NyquistLimit = 0.5f * SampleRate - 1.0f;
		NumChannels = InNumChannels;
		CutoffFrequency = -1.0f;
		Z1.Init(0.0f, NumChannels);
		Reset();
	}

	void FInterpolatedHPF::StartFrequencyInterpolation(const float InTargetFrequency, const int32 InterpLength)
	{
		CurrInterpLength = InterpLength;
		CurrInterpCounter = 0;

		if (!FMath::IsNearlyEqual(InTargetFrequency, CutoffFrequency))
		{
			CutoffFrequency = FMath::Min(InTargetFrequency, NyquistLimit);

			const float G = GetGCoefficient();
			A0Target = G / (1.0f + G);

			A0Delta = (A0Target - A0Curr) / static_cast<float>(InterpLength);
		}
	}

	void FInterpolatedHPF::ProcessAudioFrame(float* RESTRICT InputFrame, float* RESTRICT OutputFrame)
	{
		A0Curr += A0Delta; // step forward coefficient
		++CurrInterpCounter;

		for (int32 i = 0; i < NumChannels; ++i)
		{
			float* Z1Data = Z1.GetData();
			const float InputSample = InputFrame[i];
			const float Vn = (InputSample - Z1Data[i]) * A0Curr;
			const float LPF = Vn + Z1Data[i];
			Z1Data[i] = Vn + LPF;

			OutputFrame[i] = InputSample - LPF;
		}
	}

	void FInterpolatedHPF::Reset()
	{
		A0Curr = 0.0f;
		A0Delta = 0.0f;
		CurrInterpLength = 0;
		CurrInterpCounter = 0;
		ClearMemory();
	}

	void FInterpolatedHPF::ClearMemory()
	{
		Z1.Reset();
		Z1.AddZeroed(NumChannels);
	}


} // namespace Audio