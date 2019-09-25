// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/Dsp.h"

namespace Audio
{
	// Types of spectrums which can be directly derived from FFTFreqDomainData
	enum class ESpectrumType : uint8
	{
		MagnitudeSpectrum,
		PowerSpectrum
	};

	namespace FFTIntrinsics
	{
		SIGNALPROCESSING_API uint32 NextPowerOf2(uint32 Input);
	}

	enum class EWindowType : uint8
	{
		None, // No window is applied. Technically a boxcar window.
		Hamming, // Mainlobe width of -3 dB and sidelove attenuation of ~-40 dB. Good for COLA.
		Hann, // Mainlobe width of -3 dB and sidelobe attenuation of ~-30dB. Good for COLA.
		Blackman // Mainlobe width of -3 dB and sidelobe attenuation of ~-60db. Tricky for COLA.
	};

	// Utility functions for generating different types of windows. Called in FWindow::Generate.
	SIGNALPROCESSING_API void GenerateHammingWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic);
	SIGNALPROCESSING_API void GenerateHannWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic);
	SIGNALPROCESSING_API void GenerateBlackmanWindow(float* WindowBuffer, int32 NumFrames, int32 NumChannels, bool bIsPeriodic);

	// Returns the hop size in samples necessary to maintain constant overlap add.
	// For more information on COLA, see the following page:
	// https://ccrma.stanford.edu/~jos/sasp/Overlap_Add_OLA_STFT_Processing.html
	SIGNALPROCESSING_API uint32 GetCOLAHopSizeForWindow(EWindowType InType, uint32 WindowLength);

	/**
	 * Class used to generate, contain and apply a DSP window of a given type.
	 */
	class SIGNALPROCESSING_API FWindow
	{
	public:
		/**
		 * Constructor. Allocates buffer and generates window inside of it.
		 * @param InType: The type of window that should be generated.
		 * @param InNumFrames: The number of samples that should be generated divided by the number of channels.
		 * @param InNumChannels: The amount of channels that will be used in the signal this is applied to.
		 * @param bIsPeriodic: If false, the window will be symmetrical. If true, the window will be periodic.
		 *                     Generally, set this to false if using this window with an STFT, but use true
		 *                     if this window will be used on an entire, self-contained signal.
		 */
		FWindow(EWindowType InType, int32 InNumFrames, int32 InNumChannels, bool bIsPeriodic)
			: WindowType(InType)
			, NumSamples(InNumFrames * InNumChannels)
		{
			checkf(NumSamples % 4 == 0, TEXT("For performance reasons, this window's length should be a multiple of 4."));
			Generate(InNumFrames, InNumChannels, bIsPeriodic);
		}

		// Destructor. Releases memory used for window.
		~FWindow()
		{
		}

		// Apply this window to InBuffer, which is expected to be an interleaved buffer with the same amount of frames
		// and channels this window was constructed with.
		void ApplyToBuffer(float* InBuffer)
		{
			if (WindowType == EWindowType::None)
			{
				return;
			}

			check(IsAligned<float*>(InBuffer, 4));
			MultiplyBuffersInPlace(WindowBuffer.GetData(), InBuffer, NumSamples);
		}

	private:
		EWindowType WindowType;
		AlignedFloatBuffer WindowBuffer;
		int32 NumSamples;

		// Purposefully hidden constructor.
		FWindow();

		// Generate the window. Called on constructor.
		void Generate(int32 NumFrames, int32 NumChannels, bool bIsPeriodic)
		{
			if (WindowType == EWindowType::None)
			{
				return;
			}

			WindowBuffer.Reset();
			WindowBuffer.AddZeroed(NumSamples);

			switch (WindowType)
			{
			case EWindowType::Hann:
			{
				GenerateHannWindow(WindowBuffer.GetData(), NumFrames, NumChannels, bIsPeriodic);
				break;
			}
			case EWindowType::Hamming:
			{
				GenerateHammingWindow(WindowBuffer.GetData(), NumFrames, NumChannels, bIsPeriodic);
				break;
			}
			case EWindowType::Blackman:
			{
				GenerateBlackmanWindow(WindowBuffer.GetData(), NumFrames, NumChannels, bIsPeriodic);
				break;
			}
			default:
			{
				checkf(false, TEXT("Unknown window type!"));
				break;
			}
			}
		}
	};

	struct FFTTimeDomainData
	{
		float* Buffer; // Pointer to a single channel of floats.
		int32 NumSamples; // Number of samples in InBuffer divided by the number of channels. must be a power of 2.
	};

	struct FFTFreqDomainData
	{
		// arrays in which real and imaginary values will be populated.
		float* OutReal; // Should point to an already allocated array of floats that is FFTInputParams::NumSamples long.
		float* OutImag; // Should point to an already allocated array of floats that is FFTInputParams::NumSamples long.
	};

	// Performs a one-time FFT on a float buffer. Does not support complex signals.
	// This function assumes that, if you desire a window for your FFT, that window was already
	// applied to FFTInputParams.InBuffer.
	SIGNALPROCESSING_API void PerformFFT(const FFTTimeDomainData& InputParams, FFTFreqDomainData& OutputParams);
	SIGNALPROCESSING_API void PerformIFFT(FFTFreqDomainData& InputParams, FFTTimeDomainData& OutputParams);

	struct FrequencyBuffer
	{
		AlignedFloatBuffer Real;
		AlignedFloatBuffer Imag;

		void InitZeroed(int32 Num)
		{
			Real.Reset();
			Real.AddZeroed(Num);

			Imag.Reset();
			Imag.AddZeroed(Num);
		}

		void CopyFrom(const float* InReal, const float* InImag, int32 Num)
		{
			check(Num == Real.Num() && Num == Imag.Num());
			FMemory::Memcpy(Real.GetData(), InReal, Num * sizeof(float));
			FMemory::Memcpy(Imag.GetData(), InImag, Num * sizeof(float));
		}

		void CopyFrom(const FrequencyBuffer& Other)
		{
			check(Other.Real.Num() == Real.Num() && Other.Imag.Num() == Imag.Num());
			FMemory::Memcpy(Real.GetData(), Other.Real.GetData(), Other.Real.Num() * sizeof(float));
			FMemory::Memcpy(Imag.GetData(), Other.Imag.GetData(), Other.Imag.Num() * sizeof(float));
		}
	};

	// Performs an acyclic FFT correlation on FirstBuffer and Second buffer and stores the output in OutCorrelation.
	// If bCyclic is false, This function may zero pad FirstBuffer and Second Buffer as needed.
	// If bCyclic is true, FirstBuffer and SecondBuffer should have the same length, and that length should be a power of two.
	SIGNALPROCESSING_API void CrossCorrelate(AlignedFloatBuffer& FirstBuffer, AlignedFloatBuffer& SecondBuffer, AlignedFloatBuffer& OutCorrelation, bool bZeroPad = true);
	SIGNALPROCESSING_API void CrossCorrelate(AlignedFloatBuffer& FirstBuffer, AlignedFloatBuffer& SecondBuffer, FrequencyBuffer& OutCorrelation, bool bZeroPad = true);
	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, float* OutCorrelation, int32 OutCorrelationSamples);
	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, FrequencyBuffer& OutCorrelation);

	// These variations do not allocate any additional memory during the function, provided that the FrequencyBuffers are already allocated.
	SIGNALPROCESSING_API void CrossCorrelate(const float* FirstBuffer, const float* SecondBuffer, int32 NumSamples, int32 FFTSize, FrequencyBuffer& FirstBufferFrequencies, FrequencyBuffer& SecondBufferFrequencies, FrequencyBuffer& OutCorrelation);
	SIGNALPROCESSING_API void CrossCorrelate(FrequencyBuffer& FirstBufferFrequencies, FrequencyBuffer& SecondBufferFrequencies, int32 NumSamples, FrequencyBuffer& OutCorrelation);

	class SIGNALPROCESSING_API FFFTConvolver
	{
	public:
		FFFTConvolver();

		/*
		 * Applies the convolver's internal window to InputAudio. Until SetWindow is called, ProcessAudio will not affect InputAudio.
		 * InputAudio must be a power of two.
		 */
		void ProcessAudio(float* InputAudio, int32 NumSamples);

		/**
		 * Resets the filter window. NOT thread safe to call during ProcessAudio.
		 * This function can be called with a time domain impulse response, or precomputed frequency values. 
		 * FilterSize must be a power of two.
		 */ 
		void SetFilter(const float* InFilterReal, const float* InFilterImag, int32 FilterSize, int32 FFTSize);
		void SetFilter(const FrequencyBuffer& InFilterFrequencies, int32 FilterSize);
		void SetFilter(const float* TimeDomainBuffer, int32 FilterSize);
		void SetFilter(const AlignedFloatBuffer& TimeDomainBuffer);

	private:
		void ConvolveBlock(float* InputAudio, int32 NumSamples);
		void SumInCOLABuffer(float* InputAudio, int32 NumSamples);
		void SetCOLABuffer(float* InAudio, int32 NumSamples);

		FrequencyBuffer FilterFrequencies;
		FrequencyBuffer InputFrequencies;
		int32 BlockSize;


		AlignedFloatBuffer TimeDomainInputBuffer;
		AlignedFloatBuffer COLABuffer;
	};

	// Computes the power spectrum from FFTFreqDomainData. Applies a 1/(FFTSize^2) scaling to the output to 
	// maintain equal energy between original time domain data and output spectrum.  Only the first 
	// (FFTSize / 2 + 1) spectrum values are calculated. These represent the frequencies from 0 to Nyquist.
	//
	// InFrequencyData is the input frequency domain data. Generally this is created by calling PerformFFT(...)
	// FFTSize is the number of samples used when originally calculating the FFT
	// OutBuffer is an aligned buffer which will contain spectrum data. It will constain (FFTSize / 2 + 1) elements.
	SIGNALPROCESSING_API void ComputePowerSpectrum(const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer);

	// Computes the magnitude spectrum from FFTFreqDomainData. Applies a 1/FFTSize scaling to the output to 
	// maintain equal energy between original time domain data and output spectrum.  Only the first 
	// (FFTSize / 2 + 1) spectrum values are calculated. These represent the frequencies from 0 to Nyquist.
	//
	// InFrequencyData is the input frequency domain data. Generally this is created by calling PerformFFT(...)
	// FFTSize is the number of samples used when originally calculating the FFT
	// OutBuffer is an aligned buffer which will contain spectrum data. It will constain (FFTSize / 2 + 1) elements.
	SIGNALPROCESSING_API void ComputeMagnitudeSpectrum(const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer);
	
	// Computes the spectrum from FFTFreqDomainData. Applies a scaling to the output to maintain equal 
	// energy between original time domain data and output spectrum.  Only the first (FFTSize / 2 + 1)
	// spectrum values are calculated. These represent the frequencies from 0 to Nyquist.
	//
	// InSpectrumType denotes which spectrum type to calculate.
	// InFrequencyData is the input frequency domain data. Generally this is created by calling PerformFFT(...)
	// FFTSize is the number of samples used when originally calculating the FFT
	// OutBuffer is an aligned buffer which will contain spectrum data. It will constain (FFTSize / 2 + 1) elements.
	SIGNALPROCESSING_API void ComputeSpectrum(ESpectrumType InSpectrumType, const FFTFreqDomainData& InFrequencyData, int32 FFTSize, AlignedFloatBuffer& OutBuffer);
}
