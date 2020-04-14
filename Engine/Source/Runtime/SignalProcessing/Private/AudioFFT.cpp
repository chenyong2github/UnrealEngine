// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/AudioFFT.h"
#include "HAL/IConsoleManager.h"
#include "DSP/BufferVectorOperations.h"

#define IFFT_PRESERVE_COMPLEX_COMPONENT 0

static int32 FFTMethodCVar = 0;
TAutoConsoleVariable<int32> CVarFFTMethod(
	TEXT("au.dsp.FFTMethod"),
	FFTMethodCVar,
	TEXT("Determines whether we use an iterative FFT method or the DFT.\n")
	TEXT("0: Use Iterative FFT, 1:: Use DFT"),
	ECVF_Default);

namespace Audio
{

	void GenerateHammingWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic)
	{
		const int32 N = bIsPeriodic ? NumFrames : NumFrames - 1;
		const float PhaseDelta = 2.0f * PI / N;
		float Phase = 0.0f;

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			const float Value = 0.54 -  (0.46f * FMath::Cos(Phase));
			Phase += PhaseDelta;

			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
			{
				WindowBuffer[FrameIndex * NumChannels + ChannelIndex] = Value;
			}
		}
	}

	void GenerateHannWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic)
	{
		const int32 N = bIsPeriodic ? NumFrames : NumFrames - 1;
		const float PhaseDelta = 2.0f * PI / N;
		float Phase = 0.0f;

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			const float Value = 0.5f * (1 - FMath::Cos(Phase));
			Phase += PhaseDelta;

			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
			{
				WindowBuffer[FrameIndex * NumChannels + ChannelIndex] = Value;
			}
		}
	}

	void GenerateBlackmanWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic)
	{
		const int32 N = bIsPeriodic ? NumFrames : NumFrames - 1;
		const int32 Midpoint = (N % 2) ? (N + 1) / 2 : N / 2;

		const float PhaseDelta = 2.0f * PI / (N - 1);
		float Phase = 0.0f;

		// Generate the first half of the window:
		for (int32 FrameIndex = 0; FrameIndex <= Midpoint; FrameIndex++)
		{
			const float Value = 0.42f - 0.5 * FMath::Cos(Phase) + 0.08 * FMath::Cos(2 * Phase);
			Phase += PhaseDelta;

			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
			{
				WindowBuffer[FrameIndex * NumChannels + ChannelIndex] = Value;
			}
		}

		// Flip first half for the second half of the window:
		for (int32 FrameIndex = Midpoint + 1; FrameIndex < NumFrames; FrameIndex++)
		{
			const float Value = WindowBuffer[Midpoint - (FrameIndex - Midpoint)];
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
			{
				WindowBuffer[FrameIndex * NumChannels + ChannelIndex] = Value;
			}
		}
	}

	uint32 GetCOLAHopSizeForWindow(EWindowType InType, uint32 WindowLength)
	{
		switch (InType)
		{
		case EWindowType::Hann:
		case EWindowType::Hamming:
			return FMath::FloorToInt((0.5f) * WindowLength);
			break;
		case EWindowType::Blackman:
			// Optimal overlap for any Blackman window is derived in this paper:
			// http://edoc.mpg.de/395068
			return FMath::FloorToInt((0.339f) * WindowLength);
			break;
		case EWindowType::None:
		default:
			return WindowLength;
			break;
		}
	}

	namespace FFTIntrinsics
	{
		 uint32 NextPowerOf2(uint32 Input)
		{
			uint32 Value = 2;
			while (Value < Input)
				Value *= 2;
			return Value;
		}

		// Fast bit reversal helper function. Can be used if N is a power of 2. Not well exercised.
		uint32 FastBitReversal(uint32 X, uint32 N)
		{
			// Num bits:
			uint32 NBit = N; // FMath::Log2(FFTSize);

			uint32 Mask = ~0;

			while ((NBit >>= 1) > 0)
			{
				Mask ^= (Mask << NBit);
				X = ((X >> (32u - N + NBit)) & Mask) | ((X << NBit) & ~Mask);
			}

			return X;
		}

		// Slow bit reversal helper function. performs bit reversal on an index, bit by bit. N is the number of bits (Log2(FFTSize))
		uint32 SlowBitReversal(uint32 X, uint32 N)
		{
			int32 ReversedX = X;
			int32 Count = N - 1;

			X >>= 1;
			while (X > 0)
			{
				ReversedX = (ReversedX << 1) | (X & 1);
				Count--;
				X >>= 1;
			}

			return ((ReversedX << Count) & ((1 << N) - 1));
		}

		// Alternate method for SlowBitReversal. Faster when N >= 7.
		uint32 SlowBitReversal2(uint32 X, uint32 N)
		{
			X = ReverseBits(X);
			return X >> (32 - N);
		}

		void ComplexMultiply(const float AReal, const float AImag, const float BReal, const float BImag, float& OutReal, float& OutImag)
		{
			OutReal = AReal * BReal - AImag * BImag;
			OutImag = AReal * BImag + AImag * BReal;
		}

		// Given:
		// X = A + iB
		// Y = C + iD
		// This function performs the following:
		// Y = (A*C - B*D) + i (A*D + B*C)
		void ComplexMultiplyInPlace(const FrequencyBuffer& InFreqBuffer, FrequencyBuffer& OutFreqBuffer)
		{
			checkSlow(InFreqBuffer.Real.Num() % 4 == 0);
			checkSlow(InFreqBuffer.Real.Num() == OutFreqBuffer.Real.Num());
			int32 NumIterations = InFreqBuffer.Real.Num() / 4;

			
			const float* XRealBuffer = InFreqBuffer.Real.GetData();
			const float* XImagBuffer = InFreqBuffer.Imag.GetData();

			float* YRealBuffer = OutFreqBuffer.Real.GetData();
			float* YImagBuffer = OutFreqBuffer.Imag.GetData();

			for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
			{
				const VectorRegister A = VectorLoad(&XRealBuffer[Iteration * 4]);
				const VectorRegister B = VectorLoad(&XImagBuffer[Iteration * 4]);

				const VectorRegister C = VectorLoad(&YRealBuffer[Iteration * 4]);
				const VectorRegister D = VectorLoad(&YImagBuffer[Iteration * 4]);

				const VectorRegister ResultReal = VectorSubtract(VectorMultiply(A, C), VectorMultiply(B, D));
				const VectorRegister ResultImag = VectorAdd(VectorMultiply(A, D), VectorMultiply(B, C));

				VectorStore(ResultReal, &YRealBuffer[Iteration * 4]);
				VectorStore(ResultImag, &YImagBuffer[Iteration * 4]);
			}
		}

		// Given:
		// X = A + iB
		// y = c
		// This function performs the following:
		// X = (A*c) + i (B*c)
		void ComplexMultiplyInPlaceByConstant(FrequencyBuffer& InFreqBuffer, float InReal)
		{
			checkSlow(InFreqBuffer.Real.Num() % 4 == 0);
			
			MultiplyBufferByConstantInPlace(InFreqBuffer.Real, InReal);
			MultiplyBufferByConstantInPlace(InFreqBuffer.Imag, InReal);
		}

		// Given:
		// X = A + iB
		// y = c + id
		// This function performs the following:
		// X = (A*c - B*d) + i (A*d + B*C)
		void ComplexMultiplyInPlaceByConstant(FrequencyBuffer& InFreqBuffer, float InReal, float InImag)
		{
			checkSlow(InFreqBuffer.Real.Num() % 4 == 0);
			int32 NumIterations = InFreqBuffer.Real.Num() / 4;


			float* XRealBuffer = InFreqBuffer.Real.GetData();
			float* XImagBuffer = InFreqBuffer.Imag.GetData();

			const VectorRegister C = VectorSetFloat1(InReal);
			const VectorRegister D = VectorSetFloat1(InImag);

			for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
			{
				const VectorRegister A = VectorLoad(&XRealBuffer[Iteration * 4]);
				const VectorRegister B = VectorLoad(&XImagBuffer[Iteration * 4]);

				const VectorRegister ResultReal = VectorSubtract(VectorMultiply(A, C), VectorMultiply(B, D));
				const VectorRegister ResultImag = VectorAdd(VectorMultiply(A, D), VectorMultiply(B, C));

				VectorStore(ResultReal, &XRealBuffer[Iteration * 4]);
				VectorStore(ResultImag, &XImagBuffer[Iteration * 4]);
			}
		}

		// Given:
		// X = A + iB
		// Y = C + iD
		// This function performs the following:
		// Z = (A*C - B*(-D)) + i (A*(-D) + B*C)
		//   = (A*C + B*D) + i (-(A*D) + B*C)
		//   = (A*C + B*D) + i (B*C - A*D)
		void ComplexMultiplyByConjugate(const FrequencyBuffer& FirstFreqBuffer, const FrequencyBuffer& SecondFrequencyBuffer, FrequencyBuffer& OutFrequencyBuffer)
		{
			checkSlow(FirstFreqBuffer.Real.Num() % 4 == 0);
			checkSlow(FirstFreqBuffer.Real.Num() == SecondFrequencyBuffer.Real.Num());
			checkSlow(FirstFreqBuffer.Real.Num() == OutFrequencyBuffer.Real.Num());

			int32 NumIterations = FirstFreqBuffer.Real.Num() / 4;


			const float* XRealBuffer = FirstFreqBuffer.Real.GetData();
			const float* XImagBuffer = FirstFreqBuffer.Imag.GetData();

			const float* YRealBuffer = SecondFrequencyBuffer.Real.GetData();
			const float* YImagBuffer = SecondFrequencyBuffer.Imag.GetData();

			float* ZRealBuffer = OutFrequencyBuffer.Real.GetData();
			float* ZImagBuffer = OutFrequencyBuffer.Imag.GetData();

			const VectorRegister NegativeOne = VectorSetFloat1(-1.0f);

			for (int32 Iteration = 0; Iteration < NumIterations; Iteration++)
			{
				const VectorRegister A = VectorLoad(&XRealBuffer[Iteration * 4]);
				const VectorRegister B = VectorLoad(&XImagBuffer[Iteration * 4]);

				const VectorRegister C = VectorLoad(&YRealBuffer[Iteration * 4]);
				const VectorRegister D = VectorLoad(&YImagBuffer[Iteration * 4]);

				const VectorRegister ResultReal = VectorAdd(VectorMultiply(A, C), VectorMultiply(B, D));
				const VectorRegister ResultImag = VectorSubtract(VectorMultiply(B, C), VectorMultiply(A, D));

				VectorStore(ResultReal, &ZRealBuffer[Iteration * 4]);
				VectorStore(ResultImag, &ZImagBuffer[Iteration * 4]);
			}
		}

		// Separates InBuffer (assumed to be mono here)
		void SeperateInPlace(float* InBuffer, uint32 NumSamples)
		{
			check(FMath::CountBits(NumSamples) == 1)
			const uint32 NumBits = FMath::CountTrailingZeros(NumSamples);

			
			for (uint32 Index = 0; Index < NumSamples; Index++)
			{
				uint32 SwappedIndex = SlowBitReversal(Index, NumBits);
				if (Index < SwappedIndex)
				{
					Swap(InBuffer[Index], InBuffer[SwappedIndex]);
				}
			}
		}

		void SeparateIntoCopy(float* InBuffer, float* OutBuffer, uint32 NumSamples)
		{
			check(FMath::CountBits(NumSamples) == 1)
			const uint32 NumBits = FMath::CountTrailingZeros(NumSamples);

			for (uint32 Index = 0; Index < NumSamples; Index++)
			{
				const uint32 ReversedIndex = SlowBitReversal2(Index, NumBits);
				OutBuffer[ReversedIndex] = InBuffer[Index];
			}
		}

		void ComputeButterfliesInPlace(float* OutReal, float* OutImag, uint32 NumSamples)
		{
			check(FMath::CountBits(NumSamples) == 1)
			const uint32 LogNumSamples = FMath::CountTrailingZeros(NumSamples);
			
			for (uint32 S = 1; S <= LogNumSamples; S++)
			{
				const uint32 M = (1u << S);
				const uint32 M2 = M >> 1;

				// Initialize sinusoid.
				float OmegaReal = 1.0f;
				float OmegaImag = 0.0f;

				// Initialize W of M:
				float OmegaMReal = FMath::Cos(PI / M2);
				float OmegaMImag = -FMath::Sin(PI / M2);

				for (uint32 j = 0; j < M2; j++)
				{
					for (uint32 k = j; k < NumSamples; k += M)
					{
						// Compute twiddle factor:
						float TwiddleReal, TwiddleImag;
						const uint32 TwiddleIndex = k + M2;

						ComplexMultiply(OmegaReal, OmegaImag, OutReal[TwiddleIndex], OutImag[TwiddleIndex], TwiddleReal, TwiddleImag);

						// Swap even and odd indices:

						float TempReal = OutReal[k];
						float TempImag = OutImag[k];

						OutReal[k] = TempReal + TwiddleReal;
						OutImag[k] = TempImag + TwiddleImag;

						OutReal[TwiddleIndex] = TempReal - TwiddleReal;
						OutImag[TwiddleIndex] = TempImag - TwiddleImag;
					}

					// Increment phase of W:
					ComplexMultiply(OmegaReal, OmegaImag, OmegaMReal, OmegaMImag, OmegaReal, OmegaImag);
				}
			}
		}

		void ComputeButterfliesInPlace2(float* OutReal, float* OutImag, int32 NumSamples)
		{
			for (int32 BitPosition = 2; BitPosition <= NumSamples; BitPosition <<= 1)
			{
				for (int32 I = 0; I < NumSamples; I += BitPosition)
				{
					for (int32 K = 0; K < (BitPosition / 2); K++)
					{
						int32 EvenIndex = I + K;
						int32 OddIndex = I + K + (BitPosition / 2);

						float EvenReal = OutReal[EvenIndex];
						float EvenImag = OutImag[EvenIndex];

						float OddReal = OutReal[OddIndex];
						float OddImag = OutImag[OddIndex];

						float Phase = -2.0f * PI * K / ((float)BitPosition);
						float TwiddleReal = FMath::Cos(Phase);
						float TwiddleImag = -FMath::Sin(Phase);

						ComplexMultiply(TwiddleReal, TwiddleImag, OddReal, OddImag, TwiddleReal, TwiddleImag);

						// Swap even and odd indices:

						OutReal[EvenIndex] = EvenReal + TwiddleReal;
						OutImag[EvenIndex] = EvenImag + TwiddleImag;

						OutReal[OddIndex] = EvenReal - TwiddleReal;
						OutImag[OddIndex] = EvenImag - TwiddleImag;
					}
				}
			}
		}

		void PerformIterativeFFT(const FFTTimeDomainData& InputParams, FFTFreqDomainData& OutputParams)
		{
			// Separate even and odd elements into real buffer:
			SeparateIntoCopy(InputParams.Buffer, OutputParams.OutReal, InputParams.NumSamples);

			//Zero out imaginary buffer since the input signal is not complex:
			FMemory::Memzero(OutputParams.OutImag, InputParams.NumSamples * sizeof(float));

			// Iterate over and compute butterflies.
			ComputeButterfliesInPlace(OutputParams.OutReal, OutputParams.OutImag, InputParams.NumSamples);
		}

		void PerformIterativeIFFT(FFTFreqDomainData& InputParams, FFTTimeDomainData& OutputParams)
		{
			SeperateInPlace(InputParams.OutReal, OutputParams.NumSamples);
			SeperateInPlace(InputParams.OutImag, OutputParams.NumSamples);

			// IFFT can be done by performing a forward FFT on the complex conjugate of a frequency domain signal:
			MultiplyBufferByConstantInPlace(InputParams.OutImag, OutputParams.NumSamples, -1.0f);

			// Iterate over and compute butterflies.
			ComputeButterfliesInPlace(InputParams.OutReal, InputParams.OutImag, OutputParams.NumSamples);

#if IFFT_PRESERVE_COMPLEX_COMPONENT
			for (int32 Index = 0; Index < OutputParams.NumSamples; Index++)
			{
				const float Real = InputParams.OutReal[Index];
				const float Imag = InputParams.OutImag[Index];
				OutputParams.Buffer[Index] = FMath::Sqrt(Real * Real - Imag * Imag);
			}
#else
			FMemory::Memcpy(OutputParams.Buffer, InputParams.OutReal, OutputParams.NumSamples * sizeof(float));
			
			// Personal note: This is a very important step in an inverse FFT.
			Audio::MultiplyBufferByConstantInPlace(OutputParams.Buffer, OutputParams.NumSamples, 1.0f / OutputParams.NumSamples);
#endif
		}

		void PerformDFT(const FFTTimeDomainData& InputParams, FFTFreqDomainData& OutputParams)
		{
			const float* InputBuffer = InputParams.Buffer;
			float* OutReal = OutputParams.OutImag;
			float* OutImag = OutputParams.OutImag;

			float N = InputParams.NumSamples;

			for (int32 FreqIndex = 0; FreqIndex < InputParams.NumSamples; FreqIndex++)
			{
				float RealSum = 0.0f;
				float ImagSum = 0.0f;

				for (int32 TimeIndex = 0; TimeIndex < InputParams.NumSamples; TimeIndex++)
				{
					const float Exponent = FreqIndex * TimeIndex * PI * 2 / N;
					RealSum += InputBuffer[TimeIndex] * FMath::Cos(Exponent);
					ImagSum -= InputBuffer[TimeIndex] * FMath::Sin(Exponent);
				}

				OutReal[FreqIndex] = RealSum;
				OutImag[FreqIndex] = ImagSum;
			}
		}

		void PerformIDFT(const FFTFreqDomainData& InputParams, FFTTimeDomainData& OutputParams)
		{
			float* OutputBuffer = OutputParams.Buffer;
			float* InReal = InputParams.OutImag;
			float* InImag = InputParams.OutImag;

			float N = OutputParams.NumSamples;

			for (int32 TimeIndex = 0; TimeIndex < OutputParams.NumSamples; TimeIndex++)
			{
				float RealSum = 0.0f;
				float ImagSum = 0.0f;

				for (int32 FreqIndex = 0; FreqIndex < OutputParams.NumSamples; FreqIndex++)
				{
					const float Exponent = TimeIndex * FreqIndex * PI * 2 / N;
					RealSum += InReal[FreqIndex] * FMath::Cos(Exponent) - InImag[FreqIndex] * FMath::Sin(Exponent);
				}

				OutputBuffer[TimeIndex] = RealSum;
			}
		}
	}

	void PerformFFT(const FFTTimeDomainData& InputParams, FFTFreqDomainData& OutputParams)
	{
		int32 FFTMethod = CVarFFTMethod.GetValueOnAnyThread();
		if (FFTMethod)
		{
			FFTIntrinsics::PerformDFT(InputParams, OutputParams);
		}
		else
		{
			FFTIntrinsics::PerformIterativeFFT(InputParams, OutputParams);
		}
	}

	void PerformIFFT(FFTFreqDomainData& InputParams, FFTTimeDomainData& OutputParams)
	{
		int32 FFTMethod = CVarFFTMethod.GetValueOnAnyThread();
		if (FFTMethod)
		{
			FFTIntrinsics::PerformIDFT(InputParams, OutputParams);
		}
		else
		{
			FFTIntrinsics::PerformIterativeIFFT(InputParams, OutputParams);
		}
	}

	// Implementation of fft algorithm interface
	class FAudioFFTAlgorithm : public IFFTAlgorithm
	{
			const int32 FFTSize;
			const int32 NumOutputFFTElements;
			FFTFreqDomainData FreqDomainData;

			AlignedFloatBuffer FreqRealBuffer;
			AlignedFloatBuffer FreqImagBuffer;
			

		public:

			FAudioFFTAlgorithm(int32 InFFTSize)
			:	FFTSize(InFFTSize)
			,	NumOutputFFTElements((InFFTSize / 2) + 1)
			{
				// For freq domain data, we need separate buffers since we are expecting
				// interleaved [real, imag] data.
				FreqRealBuffer.AddUninitialized(FFTSize);
				FreqImagBuffer.AddUninitialized(FFTSize);

				FreqDomainData.OutReal = FreqRealBuffer.GetData();
				FreqDomainData.OutImag = FreqImagBuffer.GetData();
			}

			// virtual destructor for inheritance.
			virtual ~FAudioFFTAlgorithm() {}

			// Number of elements in FFT
			virtual int32 Size() const override { return FFTSize; }

			// Scaling applied when performing forward FFT.
			virtual EFFTScaling ForwardScaling() const { return EFFTScaling::MultipliedBySqrtFFTSize; }

			/* Scaling applied when performing inverse FFT. */
			virtual EFFTScaling InverseScaling() const { return EFFTScaling::DividedBySqrtFFTSize; }

			
			// ForwardRealToComplex
			// InReal - Array of floats to input into fourier transform. Must have FFTSize() elements.
			// OutComplex - Array of floats to store output of fourier transform. Must have (FFTSize() + 2) float elements which represent ((FFTSize() / 2) + 1) complex numbers in interleaved format.
			virtual void ForwardRealToComplex(const float* RESTRICT InReal, float* RESTRICT OutComplex) override
			{
				const FFTTimeDomainData TimeDomainData = {const_cast<float*>(InReal), FFTSize};
				PerformFFT(TimeDomainData, FreqDomainData);

				// Convert FFT output data to interleaved format.
				int32 OutPos = 0;
				for (int32 i = 0; i < NumOutputFFTElements; i++)
				{
					OutComplex[OutPos] = FreqDomainData.OutReal[i];
					OutPos++;
					OutComplex[OutPos] = FreqDomainData.OutImag[i];
					OutPos++;
				}
			}

			// InverseComplexToReal
			// InComplex - Array of floats to input into inverse fourier transform. Must have (FFTSize() + 2) float elements which represent ((FFTSize() / 2) + 1) complex numbers in interleaved format.
			// OutReal - Array of floats to store output of inverse fourier transform. Must have FFTSize() elements.
			virtual void InverseComplexToReal(const float* RESTRICT InComplex, float* RESTRICT OutReal) override
			{

				// For the complex data the phase must be flipped for negative frequencies (or frequencies above nyquist depending on the way you think about it.)
				
				// Copy from 0Hz -> Nyquist
				int32 ComplexPos = 0;
				for (int32 i = 0; i < NumOutputFFTElements; i++)
				{
					FreqDomainData.OutReal[i] = InComplex[ComplexPos];
					ComplexPos++;
					FreqDomainData.OutImag[i] = InComplex[ComplexPos];
					ComplexPos++;
				}

				// Perform mirror
				int32 InFreqPos = NumOutputFFTElements - 2;
				for (int32 MirrorPos = NumOutputFFTElements; MirrorPos < FFTSize; MirrorPos++)
				{
					FreqDomainData.OutReal[MirrorPos] = FreqDomainData.OutReal[InFreqPos];
					FreqDomainData.OutImag[MirrorPos] = -FreqDomainData.OutImag[InFreqPos];
					InFreqPos--;
				}

				FFTTimeDomainData TimeDomainData = {OutReal, FFTSize};
				PerformIFFT(FreqDomainData, TimeDomainData);
			}

			// BatchForwardRealToComplex
			virtual void BatchForwardRealToComplex(int32 InCount, const float* const RESTRICT InReal[], float* RESTRICT OutComplex[]) override
			{
				for (int32 i = 0; i < InCount; i++)
				{
					ForwardRealToComplex(InReal[i], OutComplex[i]);
				}
			}

			// BatchInverseComplexToReal
			virtual void BatchInverseComplexToReal(int32 InCount, const float* const RESTRICT InComplex[], float* RESTRICT OutReal[]) override
			{
				for (int32 i = 0; i < InCount; i++)
				{
					InverseComplexToReal(InComplex[i], OutReal[i]);
				}
			}
	};


	// FFT Algorithm factory for this FFT implementation
	FAudioFFTAlgorithmFactory::~FAudioFFTAlgorithmFactory() {}

	// Name of this fft algorithm factory. 
	FName FAudioFFTAlgorithmFactory::GetFactoryName() const 
	{
		static const FName FactoryName = FName(TEXT("OriginalFFT_Deprecated"));
		return FactoryName;
	}

	// If true, this implementation uses hardware acceleration.
	bool FAudioFFTAlgorithmFactory::IsHardwareAccelerated() const
	{
		return false;
	}

	// If true, this implementation requires input and output arrays to be 128 bit aligned.
	bool FAudioFFTAlgorithmFactory::Expects128BitAlignedArrays() const
	{
		return false;
	}

	// Returns true if the input settings are supported by this factory.
	bool FAudioFFTAlgorithmFactory::AreFFTSettingsSupported(const FFFTSettings& InSettings) const 
	{
		return (InSettings.Log2Size > 1) && (InSettings.Log2Size < 30);
	}

	// Create a new FFT algorithm.
	TUniquePtr<IFFTAlgorithm> FAudioFFTAlgorithmFactory::NewFFTAlgorithm(const FFFTSettings& InSettings)
	{
		check(AreFFTSettingsSupported(InSettings));

		// equivalent of 2^(InSettings.Log2Size)
		int32 FFTSize = 1 << InSettings.Log2Size;

		return MakeUnique<FAudioFFTAlgorithm>(FFTSize);
	}

	void ComputePowerSpectrumNoScaling(const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer)
	{
		check((FFTSize % 2) == 0);

		if (FFTSize < 1)
		{
			// Can't do anything with a zero sized fft.
			OutBuffer.Reset(0);
			return;
		}

		// Spectrum only calculates values for real positive frequencies. 
		const int32 NumSpectrumValues = (FFTSize / 2) + 1;

		// Resize output buffer
		OutBuffer.Reset(NumSpectrumValues);
		OutBuffer.AddUninitialized(NumSpectrumValues);

		float* OutBufferData = OutBuffer.GetData();

		const float* RealData = InFrequencyData.OutReal;
		const float* ImagData = InFrequencyData.OutImag;

		if (IsAligned<const float*>(RealData, AUDIO_BUFFER_ALIGNMENT) && IsAligned<const float*>(ImagData, AUDIO_BUFFER_ALIGNMENT) && (NumSpectrumValues > 5))
		{
			BufferComplexToPowerFast(RealData, ImagData, OutBufferData, NumSpectrumValues - 1);

			// Buffer operations are on 16 byte boundaries, which excludes the location of the nyquist
			// frequency in the fft output buffers. Here we explicitly handle data at nyquist freq.
			const int32 NyquistIndex = NumSpectrumValues - 1;
			const float NyquistReal = RealData[NyquistIndex];
			const float NyquistImag = ImagData[NyquistIndex];
			OutBufferData[NyquistIndex] = NyquistReal * NyquistReal + NyquistImag * NyquistImag;
		}
		else
		{
			for (int32 i = 0; i < NumSpectrumValues; i++)
			{
				OutBufferData[i] = RealData[i] * RealData[i] + ImagData[i] * ImagData[i];
			}
		}
	}

	void ComputePowerSpectrum(const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer)
	{
		if (FFTSize < 1)
		{
			OutBuffer.Reset();
			return;
		}

		ComputePowerSpectrumNoScaling(InFrequencyData, FFTSize, OutBuffer);

		const int32 NumSpectrumValues = OutBuffer.Num();
		if (NumSpectrumValues < 1)
		{
			return;
		}

		const float FFTScale = 1.f / static_cast<float>(FFTSize);

		float* OutBufferData = OutBuffer.GetData();

		if (NumSpectrumValues > 1)
		{
			MultiplyBufferByConstantInPlace(OutBufferData, NumSpectrumValues - 1, FFTScale);
		}
		
		OutBufferData[NumSpectrumValues - 1] *= FFTScale;
	}

	void ComputeMagnitudeSpectrum(const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer)
	{
		if (FFTSize < 1)
		{
			OutBuffer.Reset();
			return;
		}

		ComputePowerSpectrumNoScaling(InFrequencyData, FFTSize, OutBuffer);

		const int32 NumSpectrumValues = OutBuffer.Num();
		if (NumSpectrumValues < 1)
		{
			return;
		}

		const float FFTScale = 1.f / FMath::Sqrt(static_cast<float>(FFTSize));

		float* OutBufferData = OutBuffer.GetData();

		for (int32 i = 0; i < NumSpectrumValues; i++)
		{
			// TODO: Currently no vector sqrt. utilize fmath for now.
			OutBufferData[i] = FMath::Sqrt(OutBufferData[i]);
		}

		if (NumSpectrumValues > 1)
		{
			MultiplyBufferByConstantInPlace(OutBufferData, NumSpectrumValues - 1, FFTScale);
		}
		OutBufferData[NumSpectrumValues - 1] *= FFTScale;
	}

	void ComputeSpectrum(ESpectrumType InSpectrumType, const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer)
	{
		switch (InSpectrumType)
		{
			case ESpectrumType::MagnitudeSpectrum:
				ComputeMagnitudeSpectrum(InFrequencyData, FFTSize, OutBuffer);
				break;

			case ESpectrumType::PowerSpectrum:
				ComputePowerSpectrum(InFrequencyData, FFTSize, OutBuffer);
				break;

			default:
				checkf(false, TEXT("Unhandled Audio::ESpectrumType"));
		}
	}
	SIGNALPROCESSING_API void CrossCorrelate(AlignedFloatBuffer& FirstBuffer, AlignedFloatBuffer& SecondBuffer, AlignedFloatBuffer& OutCorrelation, bool bZeroPad /*= true*/)
	{
		FrequencyBuffer OutputCorrelationFrequencies;
		CrossCorrelate(FirstBuffer, SecondBuffer, OutputCorrelationFrequencies, bZeroPad);

		OutCorrelation.Reset();
		OutCorrelation.AddUninitialized(OutputCorrelationFrequencies.Real.Num());

		// Perform IFFT into OutCorrelation:
		FFTFreqDomainData FreqDomainData = 
		{
			OutputCorrelationFrequencies.Real.GetData(),
			OutputCorrelationFrequencies.Imag.GetData()
		};

		FFTTimeDomainData TimeDomainData =
		{
			OutCorrelation.GetData(),
			OutCorrelation.Num()
		};

		PerformIFFT(FreqDomainData, TimeDomainData);
	}

	SIGNALPROCESSING_API void CrossCorrelate(AlignedFloatBuffer& FirstBuffer, AlignedFloatBuffer& SecondBuffer, FrequencyBuffer& OutCorrelation, bool bZeroPad /*= true*/)
	{
		const int32 NumSamples = FMath::Max(FirstBuffer.Num(), SecondBuffer.Num());

		if (!bZeroPad)
		{
			int32 FFTLength = FFTIntrinsics::NextPowerOf2(NumSamples - 1);

			FirstBuffer.AddZeroed(FFTLength - FirstBuffer.Num());
			SecondBuffer.AddZeroed(FFTLength - SecondBuffer.Num());
		}
		else
		{
			checkSlow(FirstBuffer.Num() == SecondBuffer.Num() && FMath::IsPowerOfTwo(FirstBuffer.Num()));
		}

		CrossCorrelate(FirstBuffer.GetData(), SecondBuffer.GetData(), NumSamples, FirstBuffer.Num(), OutCorrelation);
	}

	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, float* OutCorrelation, int32 OutCorrelationSamples)
	{
		FrequencyBuffer OutputCorrelationFrequencies;
		CrossCorrelate(FirstBuffer, SecondBuffer, NumSamples, FFTSize, OutputCorrelationFrequencies);

		checkSlow(FFTSize == OutCorrelationSamples);

		// Perform IFFT into OutCorrelation:
		FFTFreqDomainData FreqDomainData =
		{
			OutputCorrelationFrequencies.Real.GetData(),
			OutputCorrelationFrequencies.Imag.GetData()
		};

		FFTTimeDomainData TimeDomainData =
		{
			OutCorrelation,
			OutCorrelationSamples
		};

		PerformIFFT(FreqDomainData, TimeDomainData);
	}

	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, FrequencyBuffer& OutCorrelation)
	{
		FrequencyBuffer FirstBufferFrequencies;
		FrequencyBuffer SecondBufferFrequencies;

		CrossCorrelate(FirstBuffer, SecondBuffer, NumSamples, FFTSize, FirstBufferFrequencies, SecondBufferFrequencies, OutCorrelation);
	}

	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, FrequencyBuffer& FirstBufferFrequencies, FrequencyBuffer& SecondBufferFrequencies, FrequencyBuffer& OutCorrelation)
	{
		checkSlow(FMath::IsPowerOfTwo(FFTSize));
		OutCorrelation.InitZeroed(FFTSize);
		
		// Perform FFT on First Buffer input:
		FirstBufferFrequencies.InitZeroed(FFTSize);
		FFTTimeDomainData TimeDomainData =
		{
			const_cast<float*>(FirstBuffer), // Note: Because we use the same struct for the IFFT as we do the FFT, we can't make this property const.
			FFTSize
		};

		FFTFreqDomainData FreqDomainData =
		{
			FirstBufferFrequencies.Real.GetData(),
			FirstBufferFrequencies.Imag.GetData()
		};
		
		PerformFFT(TimeDomainData, FreqDomainData);

		// Perform FFT on second buffer of input:
		SecondBufferFrequencies.InitZeroed(FFTSize);
		TimeDomainData =
		{
			const_cast<float*>(SecondBuffer),
			FFTSize
		};

		FreqDomainData = 
		{
			SecondBufferFrequencies.Real.GetData(),
			SecondBufferFrequencies.Imag.GetData()
		};

		PerformFFT(TimeDomainData, FreqDomainData);

		CrossCorrelate(FirstBufferFrequencies, SecondBufferFrequencies, NumSamples, OutCorrelation);
	}

	SIGNALPROCESSING_API void CrossCorrelate(FrequencyBuffer& FirstBufferFrequencies, FrequencyBuffer& SecondBufferFrequencies, int32 NumSamples, FrequencyBuffer& OutCorrelation)
	{
		FFTIntrinsics::ComplexMultiplyByConjugate(FirstBufferFrequencies, SecondBufferFrequencies, OutCorrelation);

		// Normalize our output by the length of the signals:
		//const float NormalizationFactor = 1.0f / NumSamples;
		//FFTIntrinsics::ComplexMultiplyInPlaceByConstant(OutCorrelation, NormalizationFactor);
	}

	FFFTConvolver::FFFTConvolver()
		: BlockSize(0)
	{
	}

	void FFFTConvolver::ConvolveBlock(float* InputAudio, int32 NumSamples)
	{
		checkSlow(NumSamples != 0);

		const int32 FFTSize = FilterFrequencies.Real.Num();

		// Zero-pad the input to the FFT size:
		TimeDomainInputBuffer.Reset(FFTSize);
		TimeDomainInputBuffer.AddZeroed(FFTSize);
		FMemory::Memcpy(TimeDomainInputBuffer.GetData(), InputAudio, NumSamples * sizeof(float));

		FFTTimeDomainData TimeDomainData = 
		{ 
			TimeDomainInputBuffer.GetData(), // Buffer
			TimeDomainInputBuffer.Num()      // FFTSize
		};

		FFTFreqDomainData FreqDomainData = 
		{
			InputFrequencies.Real.GetData(),
			InputFrequencies.Imag.GetData()
		};

		PerformFFT(TimeDomainData, FreqDomainData);
		FFTIntrinsics::ComplexMultiplyInPlace(FilterFrequencies, InputFrequencies);
		PerformIFFT(FreqDomainData, TimeDomainData);

		// Copy back from our temporary buffer to InputAudio:
		FMemory::Memcpy(InputAudio, TimeDomainInputBuffer.GetData(), NumSamples * sizeof(float));
		
		// TODO: Currently we don't support COLA buffers being larger than our block size.
		checkSlow(NumSamples >= COLABuffer.Num());
		SumInCOLABuffer(InputAudio, COLABuffer.Num());
		
		const int32 COLASize = BlockSize - 1;
		const int32 COLAOffset = NumSamples;
		SetCOLABuffer(TimeDomainInputBuffer.GetData() + COLAOffset, COLASize);
	}

	void FFFTConvolver::SumInCOLABuffer(float* InputAudio, int32 NumSamples)
	{
		const float* COLABufferPtr = COLABuffer.GetData();
		const int32 Remainder = NumSamples % 4;
		const int32 NumVectorizedSamples = NumSamples - Remainder;
		MixInBufferFast(COLABufferPtr, InputAudio, NumVectorizedSamples);
		
		for (int32 Index = NumVectorizedSamples; Index < NumSamples; Index++)
		{
			InputAudio[Index] += COLABufferPtr[Index];
		}
	}

	void FFFTConvolver::SetCOLABuffer(float* InAudio, int32 NumSamples)
	{
		COLABuffer.SetNumUninitialized(NumSamples, false);
		FMemory::Memcpy(COLABuffer.GetData(), InAudio, NumSamples * sizeof(float));
	}

	void FFFTConvolver::ProcessAudio(float* InputAudio, int32 NumSamples)
	{
		checkSlow(BlockSize != 0);

		const int32 NumFullBlocks = NumSamples / BlockSize;
		const int32 BlockRemainer = NumSamples % BlockSize;

		for (int32 Index = 0; Index < NumFullBlocks; Index++)
		{	
			ConvolveBlock(&InputAudio[BlockSize * Index], BlockSize);
		}

		if (BlockRemainer)
		{
			// Perform Convolve Block on the last buffer:
			ConvolveBlock(&InputAudio[BlockSize* NumFullBlocks], BlockRemainer);
		}
	}

	void FFFTConvolver::SetFilter(const float* InWindowReal, const float* InWindowImag, int32 FilterSize, int32 FFTSize)
	{
		checkSlow(FilterSize != 0);

		// TODO: Support non-power of two WindowSizes. To do this we'll need to be able to accumulate COLA and add partial COLA buffers to individual blocks.
		check(FMath::IsPowerOfTwo(FFTSize) && FMath::IsPowerOfTwo(FilterSize) &&  FFTSize >= FilterSize * 2 - 1);

		if (FFTSize != FilterFrequencies.Real.Num())
		{
			FilterFrequencies.InitZeroed(FFTSize);
		}

		FilterFrequencies.CopyFrom(InWindowReal, InWindowImag, FFTSize);
		BlockSize = FilterSize;

		InputFrequencies.InitZeroed(FFTSize);
		COLABuffer.Reset(BlockSize);
		COLABuffer.AddZeroed(BlockSize);
	}

	void FFFTConvolver::SetFilter(const FrequencyBuffer& InFilterFrequencies, int32 FilterSize)
	{
		checkSlow(FilterSize != 0);

		const int32 FilterFFTSize = InFilterFrequencies.Real.Num();
		checkSlow(FMath::IsPowerOfTwo(FilterFFTSize) && FilterFFTSize >= FilterSize * 2 - 1);

		if (FilterFFTSize != FilterFrequencies.Real.Num())
		{
			FilterFrequencies.InitZeroed(FilterFFTSize);
		}

		FilterFrequencies.CopyFrom(InFilterFrequencies);
		BlockSize = FilterSize;

		InputFrequencies.InitZeroed(FilterFFTSize);
		COLABuffer.Reset(BlockSize);
		COLABuffer.AddZeroed(BlockSize);
	}

	void FFFTConvolver::SetFilter(const float* TimeDomainBuffer, int32 FilterSize)
	{
		checkSlow(FilterSize != 0);

		int32 FilterFFTSize = FFTIntrinsics::NextPowerOf2(FilterSize * 2 - 1);
		checkSlow(FMath::IsPowerOfTwo(FilterFFTSize) && FilterFFTSize >= FilterSize * 2 - 1);

		FilterFrequencies.InitZeroed(FilterFFTSize);
		TimeDomainInputBuffer.Reset(FilterFFTSize);
		TimeDomainInputBuffer.AddZeroed(FilterFFTSize);
		FMemory::Memcpy(TimeDomainInputBuffer.GetData(), TimeDomainBuffer, FilterSize * sizeof(float));

		FFTTimeDomainData TimeDomainData =
		{
			TimeDomainInputBuffer.GetData(),
			TimeDomainInputBuffer.Num()
		};

		FFTFreqDomainData FreqDomainData =
		{
			FilterFrequencies.Real.GetData(),
			FilterFrequencies.Imag.GetData()
		};

		PerformFFT(TimeDomainData, FreqDomainData);
		BlockSize = FilterSize;
		InputFrequencies.InitZeroed(FilterFFTSize);
		COLABuffer.Reset(BlockSize);
		COLABuffer.AddZeroed(BlockSize);
	}

	void FFFTConvolver::SetFilter(const AlignedFloatBuffer& TimeDomainBuffer)
	{
		SetFilter(TimeDomainBuffer.GetData(), TimeDomainBuffer.Num());
	}
}
