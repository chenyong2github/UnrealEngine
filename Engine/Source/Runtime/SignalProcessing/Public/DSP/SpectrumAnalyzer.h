// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/Dsp.h"
#include "DSP/AudioFFT.h"
#include "DSP/BufferVectorOperations.h"
#include "SampleBuffer.h"
#include "Async/AsyncWork.h"

namespace Audio
{
	class IFFTAlgorithm;
	class FSpectrumAnalyzer;
	struct SIGNALPROCESSING_API FSpectrumAnalyzerSettings
	{
		// Actual FFT size used. For FSpectrumAnalyzer, we never zero pad the input buffer.
		enum class EFFTSize : uint16
		{
			Default = 512,
			TestingMin_8 = 8,
			Min_64 = 64,
			Small_256 = 256,
			Medium_512 = 512,
			Large_1024 = 1024,
			VeryLarge_2048 = 2048,
			TestLarge_4096 = 4096
		};

		// Peak interpolation method. If the EFFTSize is small but will be densely sampled,
		// it's worth using a linear or quadratic interpolation method.
		enum class EPeakInterpolationMethod : uint8
		{
			NearestNeighbor,
			Linear,
			Quadratic,
			ConstantQ,
		};

		enum class ESpectrumAnalyzerType : uint8
		{
			Magnitude,
			Power,
			Decibel,
		};

		EWindowType WindowType;
		EFFTSize FFTSize;
		EPeakInterpolationMethod InterpolationMethod;
		ESpectrumAnalyzerType  SpectrumType;

		/**
			* Hop size as a percentage of FFTSize.
			* 1.0 indicates a full hop.
			* Keeping this as 0.0 will use whatever hop size
			* can be used for WindowType to maintain COLA.
			*/
		float HopSize;

		FSpectrumAnalyzerSettings()
			: WindowType(EWindowType::Hann)
			, FFTSize(EFFTSize::Default)
			, InterpolationMethod(EPeakInterpolationMethod::Linear)
			, SpectrumType(ESpectrumAnalyzerType::Magnitude)
			, HopSize(0.0f)
		{}
	};

	/** Settings for band extractor. */
	struct SIGNALPROCESSING_API FSpectrumBandExtractorSettings
	{
		/** Sample rate of audio */
		float SampleRate; 

		/** Size of fft used in spectrum analyzer */
		int32 FFTSize; 

		/** Forward scaling of FFT used in spectrum analyzer */
		EFFTScaling FFTScaling; 

		/** Window used when perform FFT */
		EWindowType WindowType;


		/** Compare whether two settings structures are equal. */
		bool operator==(const FSpectrumBandExtractorSettings& Other) const
		{
			bool bIsEqual = ((SampleRate == Other.SampleRate)
					&& (FFTSize == Other.FFTSize)
					&& (FFTScaling == Other.FFTScaling)
					&& (WindowType == Other.WindowType));
			return bIsEqual;
		}

		/** Compare whether two settings structures are not equal. */
		bool operator!=(const FSpectrumBandExtractorSettings& Other) const
		{
			return !(*this == Other);
		}
	};

	/** Interface for spectrum band extractors.
	 *
	 *  The SpectrumBandExtractor allows for band information
	 *  to be maintained across multiple calls to retrieve bands values.
	 *  By maintaining band information across multiple calls, some intermediate 
	 *  values can be cached to speed up the operation.
	 */
	class SIGNALPROCESSING_API ISpectrumBandExtractor
	{
		public:
			/** Metric for output band values. */
			enum class EMetric : uint8
			{
				/** Return the magnitude spectrum value. */
				Magnitude,

				/** Return the power spectrum value. */
				Power,

				/** Return the decibel spectrum value. Decibels are calculated
				 * with 0dB equal to 1.f magnitude.  */
				Decibel
			};

			virtual ~ISpectrumBandExtractor() {}

			/** Set the settings and update cached internal values if needed */
			virtual void SetSettings(const FSpectrumBandExtractorSettings& InSettings) = 0;
			
			/** Removes all added bands. */
			virtual void RemoveAllBands() = 0;

			/** Returns the total number of bands. */
			virtual int32 GetNumBands() const = 0;

			/** Adds a band which calculates the band value as the value of the FFT bin nearest to the center frequency.
			 *
			 * @param InCenterFrequency - Frequency of interest in hz .
			 * @param InMetric - Metric used to calculate return value.
			 * @param InDecibelNoiseFloor - If the metric is Decibel, this is the minimum decibel value allowed.
			 * @param bInDoNormalize - If true, all values are scaled and clamped between 0.0 and 1.f. In the 
			 *                         case of Decibels, 0.0 corresponds to the decibel noise floor and 1.f to 0dB.
			 */
			virtual void AddNearestNeighborBand(float InCenterFrequency, EMetric InMetric = EMetric::Decibel, float InDecibelNoiseFloor=-40.f, bool bInDoNormalize=true) = 0;

			/** Adds a band which calculates the band value as a linear interpolation of the values of the FFT 
			 *  bins adjacent to the center frequency.
			 *
			 * @param InCenterFrequency - Frequency of interest in hz .
			 * @param InMetric - Metric used to calculate return value.
			 * @param InDecibelNoiseFloor - If the metric is Decibel, this is the minimum decibel value allowed.
			 * @param bInDoNormalize - If true, all values are scaled and clamped between 0.0 and 1.f. In the 
			 *                         case of Decibels, 0.0 corresponds to the decibel noise floor and 1.f to 0dB.
			 */
			virtual void AddLerpBand(float InCenterFrequency, EMetric InMetric = EMetric::Decibel, float InDecibelNoiseFloor=-40.f, bool bInDoNormalize=true) = 0;

			/** Adds a band which calculates the band value as a quadratic interpolation of the values of the FFT 
			 *  bins adjacent to the center frequency.
			 *
			 * @param InCenterFrequency - Frequency of interest in hz .
			 * @param InMetric - Metric used to calculate return value.
			 * @param InDecibelNoiseFloor - If the metric is Decibel, this is the minimum decibel value allowed.
			 * @param bInDoNormalize - If true, all values are scaled and clamped between 0.0 and 1.f. In the 
			 *                         case of Decibels, 0.0 corresponds to the decibel noise floor and 1.f to 0dB.
			 */
			virtual void AddQuadraticBand(float InCenterFrequency, EMetric InMetric = EMetric::Decibel, float InDecibelNoiseFloor=-40.f, bool bInDoNormalize=true) = 0;


			/** Adds a band which calculates the band value as a pesudo constant q band derived from an FFT power spectrum.
			 *
			 * @param InCenterFrequency - Frequency of interest in hz.
			 * @param InQFactor - QFactor used in band calculation. QFactor = CenterFreq / BandWidth. A small QFactor results in a wide band.
			 * @param InMetric - Metric used to calculate return value.
			 * @param InDecibelNoiseFloor - If the metric is Decibel, this is the minimum decibel value allowed.
			 * @param bInDoNormalize - If true, all values are scaled and clamped between 0.0 and 1.f. In the 
			 *                         case of Decibels, 0.0 corresponds to the decibel noise floor and 1.f to 0dB.
			 */
			virtual void AddConstantQBand(float InCenterFrequency, float InQFactor, EMetric InMetric = EMetric::Decibel, float InDecibelNoiseFloor=-40.f, bool bInDoNormalize=true) = 0;

			/** Extract the bands from a complex frequency buffer.
			 *
			 * @param InComplexBuffer - Buffer of complex frequency data from a FFT.
			 * @param OutValues - Array to store output bands.
			 */
			virtual void ExtractBands(const AlignedFloatBuffer& InComplexBuffer, TArray<float>& OutValues) = 0;

			/** Creates a ISpectrumBandExtractor. */
			static TUniquePtr<ISpectrumBandExtractor> CreateSpectrumBandExtractor(const FSpectrumBandExtractorSettings& InSettings);
	};

	/**
	 * This class locks an input buffer (for writing) and an output buffer (for reading).
	 * Uses triple buffering semantics.
	 */
	class FSpectrumAnalyzerBuffer
	{
	public:
		FSpectrumAnalyzerBuffer();
		FSpectrumAnalyzerBuffer(int32 InNum);

		void Reset(int32 InNum);

		// Input. Used on analysis thread to lock a buffer to write to.
		AlignedFloatBuffer& StartWorkOnBuffer();
		void StopWorkOnBuffer();
		
		// Output. Used to lock the most recent buffer we analyzed.
		const AlignedFloatBuffer& LockMostRecentBuffer() const;
		void UnlockBuffer();

	private:
		TArray<AlignedFloatBuffer> ComplexBuffers;

		// Private functions. Either increments or decrements the respective counter,
		// based on which index is currently in use. Mutually locked.
		void IncrementInputIndex();
		void IncrementOutputIndex();

		volatile int32 OutputIndex;
		volatile int32 InputIndex;

		// This mutex is locked when we increment either the input or output index.
		FCriticalSection BufferIndicesCriticalSection;
	};

	class FSpectrumAnalysisAsyncWorker : public FNonAbandonableTask
	{
	protected:
		FSpectrumAnalyzer* Analyzer;
		bool bUseLatestAudio;

	public:
		FSpectrumAnalysisAsyncWorker(FSpectrumAnalyzer* InAnalyzer, bool bInUseLatestAudio)
			: Analyzer(InAnalyzer)
			, bUseLatestAudio(bInUseLatestAudio)
		{}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FSpectrumAnalysisAsyncWorker, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork();

	private:
		FSpectrumAnalysisAsyncWorker();
	};

	typedef FAsyncTask<FSpectrumAnalysisAsyncWorker> FSpectrumAnalyzerTask;

	/**
	 * Class built to be a rolling spectrum analyzer for arbitrary, monaural audio data.
	 * Class is meant to scale accuracy with CPU and memory budgets.
	 * Typical usage is to either call PushAudio() and then PerformAnalysisIfPossible immediately afterwards,
	 * or have a seperate thread call PerformAnalysisIfPossible().
	 */
	class SIGNALPROCESSING_API FSpectrumAnalyzer
	{
	public:
		// If an instance is created using the default constructor, Init() must be called before it is used.
		FSpectrumAnalyzer();

		// If an instance is created using either of these constructors, Init() is not neccessary.
		FSpectrumAnalyzer(float InSampleRate);
		FSpectrumAnalyzer(const FSpectrumAnalyzerSettings& InSettings, float InSampleRate);

		~FSpectrumAnalyzer();

		// Initialize sample rate of analyzer if not known at time of construction
		void Init(float InSampleRate);
		void Init(const FSpectrumAnalyzerSettings& InSettings, float InSampleRate);

		// Update the settings used by this Spectrum Analyzer. Safe to call on any thread, but should not be called every tick.
		void SetSettings(const FSpectrumAnalyzerSettings& InSettings);

		// Get the current settings used by this Spectrum Analyzer.
		void GetSettings(FSpectrumAnalyzerSettings& OutSettings);

		// Samples magnitude (linearly) for a given frequency, in Hz.
		float GetMagnitudeForFrequency(float InFrequency);

		// Samples phase for a given frequency, in Hz.
		float GetPhaseForFrequency(float InFrequency);

		// Return array of bands using spectrum band extractor.
		void GetBands(ISpectrumBandExtractor& InExtractor, TArray<float>& OutValues);

		// You can call this function to ensure that you're sampling the same window of frequency data,
		// Then call UnlockOutputBuffer when you're done.
		// Otherwise, GetMagnitudeForFrequency and GetPhaseForFrequency will always use the latest window
		// of frequency data.
		void LockOutputBuffer();
		void UnlockOutputBuffer();
		
		// Push audio to queue. Returns false if the queue is already full.
		bool PushAudio(const TSampleBuffer<float>& InBuffer);
		bool PushAudio(const float* InBuffer, int32 NumSamples);

		// Thread safe call to perform actual FFT. Returns true if it performed the FFT, false otherwise.
		// If bAsync is true, this function will kick off an async task.
		// If bUseLatestAudio is set to true, this function will flush the entire input buffer, potentially losing data.
		// Otherwise it will only consume enough samples necessary to perform a single FFT.
		bool PerformAnalysisIfPossible(bool bUseLatestAudio = false, bool bAsync = false);

		// Returns false if this instance of FSpectrumAnalyzer was constructed with the default constructor 
		// and Init() has not been called yet.
		bool IsInitialized();

	private:

		// Called on analysis thread.
		void ResetSettings();

		// Called in GetMagnitudeForFrequency and GetPhaseForFrequency.
		void PerformInterpolation(const AlignedFloatBuffer& InComplexBuffer, FSpectrumAnalyzerSettings::EPeakInterpolationMethod InMethod, const float InFreq, float& OutReal, float& OutImag);

		// Cached current settings. Only actually used in ResetSettings().
		FSpectrumAnalyzerSettings CurrentSettings;
		volatile bool bSettingsWereUpdated;

		volatile bool bIsInitialized;

		float SampleRate;

		// Cached window that is applied prior to running the FFT.
		FWindow Window;
		int32 FFTSize;
		int32 HopInSamples;
		EFFTScaling FFTScaling;

		AlignedFloatBuffer AnalysisTimeDomainBuffer;
		TCircularAudioBuffer<float> InputQueue;
		FSpectrumAnalyzerBuffer FrequencyBuffer;

		// if non-null, owns pointer to locked frequency vector we're using.
		const AlignedFloatBuffer* LockedFrequencyVector;

		// This is used if PerformAnalysisIfPossible is called
		// with bAsync = true.
		TUniquePtr<FSpectrumAnalyzerTask> AsyncAnalysisTask;

		TUniquePtr<IFFTAlgorithm> FFT;
	};

	class SIGNALPROCESSING_API FSpectrumAnalyzerScopeLock
	{
	public:
		FSpectrumAnalyzerScopeLock(FSpectrumAnalyzer* InAnalyzer)
			: Analyzer(InAnalyzer)
		{
			Analyzer->LockOutputBuffer();
		}

		~FSpectrumAnalyzerScopeLock()
		{
			Analyzer->UnlockOutputBuffer();
		}

	private:
		FSpectrumAnalyzer* Analyzer;
	};
}
