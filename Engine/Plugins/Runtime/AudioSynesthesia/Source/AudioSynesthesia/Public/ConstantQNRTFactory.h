// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioAnalyzerNRTInterface.h"
#include "DSP/ConstantQ.h"
#include "DSP/SlidingWindow.h"
#include "DSP/FloatArrayMath.h"
#include "ConstantQAnalyzer.h"

namespace Audio
{
	/** FConstantQNRTSettings
	 *
	 *  Settings for the Constant Q Non-Real-Time Factory.
	 */
	class AUDIOSYNESTHESIA_API FConstantQNRTSettings : public IAnalyzerNRTSettings, public FConstantQAnalyzerSettings 
	{
	public:

		/** Time, in seconds, between constant Q frames. */
		float AnalysisPeriod;

		/** 
		 * If true, all channels are mixed together with equal power before analysis. Only one channel
		 * is produced at channel index 0.
		 * If false, then each channel is analyzed separately. 
		 */
		bool bDownmixToMono;

		FConstantQNRTSettings()
		:	AnalysisPeriod(0.01f)
		,	bDownmixToMono(false)
		{}
	};


	/** FConstantQFrame
	 * 
	 *  Contains Constant Q data relating to one audio window.
	 */
	struct AUDIOSYNESTHESIA_API FConstantQFrame
	{
		/** Audio channel which produced the data. */
		int32 Channel;
		
		/** Timestamp in seconds referring to the center of the audio window. */
		float Timestamp;

		/** Output spectral data */
		TArray<float> Spectrum;

		FConstantQFrame()
		:	Channel(0)
		,	Timestamp(0.f)
		{}

		FConstantQFrame(int32 InChannelIndex, float InTimestamp, TArrayView<const float> InSpectrum)
		:	Channel(InChannelIndex)
		,	Timestamp(InTimestamp)
		,	Spectrum(InSpectrum.GetData(), InSpectrum.Num())
		{}
	};

	/** Serialize FConstantQFrame */
	AUDIOSYNESTHESIA_API FArchive &operator <<(FArchive& Ar, FConstantQFrame& Frame);

	/** FConstantQNRTResult
	 *
	 *  FConstantQNRTResult is a container for the output of the FConstantQNRTWorker.
	 */
	class AUDIOSYNESTHESIA_API FConstantQNRTResult : public IAnalyzerNRTResult
	{
	public:

		FConstantQNRTResult();

		/** Serialize or unserialize object */
		void Serialize(FArchive& Archive) override;

		/** Add a single frame of CQT data */
		void AddFrame(int32 InChannelIndex, float InTimestamp, TArrayView<const float> InSpectrum);

		/** Retrieve the array of frames for a single channel of audio. */
		const TArray<FConstantQFrame>& GetFramesForChannel(int32 InChannelIndex) const;

		/** Retrieve the difference between the maximum and minimum value in the spectrum. If 
		 *  the minimum value is less than InNoiseFloor, then the result will be the difference
		 *  betwen the maximum value and the noise floor. 
		 */
		float GetChannelConstantQRange(int32 InChannelIdx, float InNoiseFloor) const;

		/** Retrieve the number of channels available in the result. */
		int32 GetNumChannels() const;

	 	/** Returns true if FConstantQFrame arrays are sorted in chronologically ascending order via their timestamp.  */
		bool IsSortedChronologically() const;

		/** Sorts FConstantQFrame arrays in chronologically ascnding order via their timestamp.  */
		void SortChronologically();

	private:

		TMap<int32, TArray<FConstantQFrame> > ChannelCQTFrames;

		TMap<int32, FFloatInterval> ChannelCQTIntervals;

		bool bIsSortedChronologically;
	};

	/** FConstantQNRTWorker
	 *
	 *  FConstantQNRTWorker computes a FConstantQNRTResult from audio samples.
	 */
	class AUDIOSYNESTHESIA_API FConstantQNRTWorker : public IAnalyzerNRTWorker
	{
	public:

		/** Constructor
		 *
		 * InParams are the parameters which describe characteristics of the input audio.
		 * InAnalyzerSettings are the settings which control various aspects of the algorithm.
		 */
		FConstantQNRTWorker(const FAnalyzerNRTParameters& InParams, const FConstantQNRTSettings& InAnalyzerSettings);

		/**
		 *  Analyze audio and put results into results pointer.
		 *
		 *  InAudio is an array view of audio.
		 *  OutResult is a pointer to a valid FConstantQNRTResult
		 */
		void Analyze(TArrayView<const float> InAudio, IAnalyzerNRTResult* OutResult) override;

		/** 
		 *  Call when analysis of audio asset is complete. 
		 *
		 *  OutResult must be a pointer to a valid FConstantQNRTResult. 
		 */
		void Finalize(IAnalyzerNRTResult* OutResult) override;

	private:

		/** Analyze audio with multiple channels interleaved. */
		void AnalyzeMultichannel(TArrayView<const float> InAudio, IAnalyzerNRTResult* OutResult, bool bDoFlush);

		/** Analyze a single window of audio from a single channel */
		void AnalyzeWindow(const AlignedFloatBuffer& InWindow, int32 InChannelIndex, FConstantQNRTResult& OutResult);

		int32 NumChannels;
		int32 NumBuffers;
		float SampleRate;
		int32 NumHopFrames;
		int32 NumHopSamples;
		int32 NumWindowFrames;
		int32 NumWindowSamples;

		float MonoScaling;

		AlignedFloatBuffer HopBuffer;
		AlignedFloatBuffer ChannelBuffer;
		AlignedFloatBuffer MonoBuffer;

		TArray<float> CQTSpectrum;

		TUniquePtr<TSlidingBuffer<float> > SlidingBuffer;
		TUniquePtr<FConstantQAnalyzer> ConstantQAnalyzer;

		bool bDownmixToMono;
	};

	/** FConstantQNRTFactory
	 *  
	 *  Factory for creating FConstantQNRT workers and results
	 */
	class AUDIOSYNESTHESIA_API FConstantQNRTFactory : public IAnalyzerNRTFactory
	{
	public:

		/** Name of this analyzer type. */
		FName GetName() const override;

		/** Human readable name of this analyzer. */
		FString GetTitle() const override;

		/** Create a new FConstantQNRTResult. */
		TUniquePtr<IAnalyzerNRTResult> NewResult() override;

		/** Create a new FConstantQNRTWorker 
		 *
		 *  InSettings must be a pointer to FConstantQNRTSetting
		 */
		TUniquePtr<IAnalyzerNRTWorker> NewWorker(const FAnalyzerNRTParameters& InParams, const IAnalyzerNRTSettings* InSettings) override;
	};
}
