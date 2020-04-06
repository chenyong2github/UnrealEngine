// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/SpectrumAnalyzer.h"
#include "DSP/FFTAlgorithm.h"
#include "DSP/ConstantQ.h"
#include "SignalProcessingModule.h"

namespace Audio
{
	namespace SpectrumAnalyzerIntrinsics
	{
		// Bit mask for returning even numbers of int32
		const int32 EvenNumberMask = 0xFFFFFFFE;

		// Constant useful for calculating log10
		const float Loge10 = FMath::Loge(10.f);
	}


	// Implementation of spectrum band extractor
	class FSpectrumBandExtractor : public ISpectrumBandExtractor
	{
			// FBandSpec describes specifications for a single band.
			struct FBandSpec
			{
				// Location in output array where band value should be stored.
				int32 OutIndex;

				// Center frequency of the band.
				float CenterFrequency;

				// The metric used for the band value.
				EMetric Metric;

				// The noisefloor in decibels, used when the metric is decibels. 
				float DbNoiseFloor;

				// The scaling parameter to apply to the power spectrum.
				float PowerSpectrumScale;

				// If true, all values are scaled and clamped between 0.0 and 1.0.
				bool bDoNormalize;

				FBandSpec(const FSpectrumBandExtractorSettings& InSettings, int32 InOutIndex, float InCenterFrequency, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize)
				:	OutIndex(InOutIndex)
				,	CenterFrequency(InCenterFrequency)
				,	Metric(InMetric)
				,	DbNoiseFloor(InDecibelNoiseFloor)
				,	PowerSpectrumScale(1.f)
				,	bDoNormalize(bInDoNormalize)
				{
					Update(InSettings);
				}

				virtual ~FBandSpec() {}

				// Update calculates parameters that are specific to FFT implementation
				// and sample rate.
				virtual void Update(const FSpectrumBandExtractorSettings& InSettings)
				{
					PowerSpectrumScale = 1.f;
					float FloatFFTSize = FMath::Max(static_cast<float>(InSettings.FFTSize), 1.f);

					switch (InSettings.FFTScaling)
					{
						case EFFTScaling::MultipliedByFFTSize:
							PowerSpectrumScale = 1.f / (FloatFFTSize * FloatFFTSize);
							break;

						case EFFTScaling::MultipliedBySqrtFFTSize:
							PowerSpectrumScale = 1.f / FloatFFTSize;
							break;

						case EFFTScaling::DividedByFFTSize:
							PowerSpectrumScale = FloatFFTSize * FloatFFTSize;
							break;

						case EFFTScaling::DividedBySqrtFFTSize:
							PowerSpectrumScale = FloatFFTSize;
							break;

						case EFFTScaling::None:
						default:
							PowerSpectrumScale = 1.f;
							break;
					}

					/*
					float WindowScale = 1.f;


					switch (InSettings.WindowType)
					{
						case EWindowType::None:
							WindowScale = 1.f / FloatFFTSize;
							break;

						case EWindowType::Hamming:
							// General form of scaling for generalized cosine on powers spectrum is
							// 1 / ((1.5 * alpha^2 - alpha + 0.5) * FFTSize)

							// For hamming, alpha = 0.54
							WindowScale = 1.f / (0.3974 * FloatFFTSize);
							break;

						case EWindowType::Hann:
							// General form of scaling for generalized cosine on powers spectrum is
							// 1 / ((1.5 * alpha^2 - alpha + 0.5) * FFTSize)

							// For hann, alpha = 0.5
							WindowScale = 1.f / (0.375f * FloatFFTSize);
							break;

						case EWindowType::Blackman:
							// General form of scaling for blackman windows is
							// 1 / ((alpha_0^2 + alpha_1^2 / 2 + alpha_2^2 / 2) * FFTSize
							//
							// For this window alpha_0 = 0.42, alpha_1 = 0.5 and alph_2 = 0.08
							WindowScale = 1.f / (0.3046f * FloatFFTSize);
							break;

						default:
							// Should not get here. Means that a window type is not covered.
							UE_LOG(LogSignalProcessing, Warning, TEXT("Invalid window type enum encountered, %d"), (int32)InSettings.WindowType);
							WindowScale = 1.f / FloatFFTSize;
							break;

					}

					PowerSpectrumScale *= WindowScale;
					*/
				}
			};

			// Specification for a nearest neighbor band.
			struct FNNBandSpec : public FBandSpec
			{
				// Use parent constructor
				using FBandSpec::FBandSpec;

				// Index in power spectrum to lookup band.
				int32 Index;

				virtual void Update(const FSpectrumBandExtractorSettings& InSettings) override
				{
					// Call parent class update.
					FBandSpec::Update(InSettings);

					// Update the index
					const int32 MaxSpectrumIndex = InSettings.FFTSize / 2;
					const float Position = CenterFrequency / FMath::Max(InSettings.SampleRate, 1.f) * InSettings.FFTSize;
					Index = FMath::RoundToInt(Position);
					Index = FMath::Clamp(Index, 0, MaxSpectrumIndex);
				}
			};

			// Specification for a linearly interpolated band.
			struct FLerpBandSpec : public FBandSpec
			{
				// Use parent constructor
				using FBandSpec::FBandSpec;

				// Lower index power spectrum.
				int32 LowerIndex;

				// Upper index of power spectrum.
				int32 UpperIndex;

				// Value used for lerping between lower and upper band values.
				float Alpha;

				virtual void Update(const FSpectrumBandExtractorSettings& InSettings) override
				{
					// Call parent class update.
					FBandSpec::Update(InSettings);

					// Update lower index, upper index and alpha.
					const int32 MaxSpectrumIndex = InSettings.FFTSize / 2;
					const float Position = CenterFrequency / FMath::Max(InSettings.SampleRate, 1.f) * InSettings.FFTSize;

					LowerIndex = FMath::FloorToInt(Position);
					UpperIndex = LowerIndex + 1;
					Alpha = Position - LowerIndex;

					LowerIndex = FMath::Clamp(LowerIndex, 0, MaxSpectrumIndex);
					UpperIndex = FMath::Clamp(UpperIndex, 0, MaxSpectrumIndex);
					Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
				}
			};

			// Specification for band using quadratic interpolation.
			struct FQuadraticBandSpec : public FBandSpec
			{
				// Use parent constructor
				using FBandSpec::FBandSpec;

				// Lower index of power spectrum used for interpolation.
				int32 LowerIndex;

				// Middle index of power spectrum used for interpolation.
				int32 MidIndex;

				// Upper index of power spectrum used for interpolation.
				int32 UpperIndex;

				// Weight for lower value.
				float LowerWeight;

				// Weight for middle value.
				float MidWeight;

				// Weight for upper value.
				float UpperWeight;

				virtual void Update(const FSpectrumBandExtractorSettings& InSettings) override
				{
					// Call parent class update.
					FBandSpec::Update(InSettings);

					// Update indices and weights.
					const int32 MaxSpectrumIndex = InSettings.FFTSize / 2;
					const float Position = CenterFrequency / FMath::Max(InSettings.SampleRate, 1.f) * InSettings.FFTSize;

					MidIndex = FMath::RoundToInt(Position);
					LowerIndex = MidIndex - 1;
					UpperIndex = MidIndex + 1;

					// Calculate polynomail weights 
					float RelativePosition = Position - LowerIndex;

					LowerWeight = ((RelativePosition - 1.f) * (RelativePosition - 2.f)) / 2.f;
					MidWeight = (RelativePosition * (RelativePosition - 2.f)) / -1.f;
					UpperWeight = (RelativePosition * (RelativePosition - 1.f)) / 2.f;

					LowerIndex = FMath::Clamp(LowerIndex, 0, MaxSpectrumIndex);
					MidIndex = FMath::Clamp(MidIndex, 0, MaxSpectrumIndex);
					UpperIndex = FMath::Clamp(UpperIndex, 0, MaxSpectrumIndex);
				}
			};

			// Specification for band using CQT band.
			struct FCQTBandSpec : public FBandSpec
			{
				// Use parent constructor
				using FBandSpec::FBandSpec;

				// QFactor controls the band width.
				float QFactor;
				
				// Start index in power spectrum 
				int32 StartIndex;

				// Weights (offset by start index) to apply to power spectrum
				AlignedFloatBuffer Weights;

				// Internal buffer used when calculating band.
				mutable AlignedFloatBuffer WorkBuffer;

				virtual void Update(const FSpectrumBandExtractorSettings& InSettings) override
				{
					// Call parent class update.
					FBandSpec::Update(InSettings);

					// Update band weights and offset index.
					const int32 MaxSpectrumIndex = InSettings.FFTSize / 2;
					const float Position = CenterFrequency / FMath::Max(InSettings.SampleRate, 1.f) * InSettings.FFTSize;
					FPseudoConstantQBandSettings CQTBandSettings;
					CQTBandSettings.CenterFreq = CenterFrequency;
					CQTBandSettings.BandWidth = FMath::Max(SMALL_NUMBER, CenterFrequency / FMath::Max(SMALL_NUMBER, QFactor));
					CQTBandSettings.FFTSize = InSettings.FFTSize;
					CQTBandSettings.SampleRate = FMath::Max(1.f, InSettings.SampleRate);
					CQTBandSettings.Normalization = EPseudoConstantQNormalization::EqualEnergy;

					StartIndex = 0;
					Weights.Reset();
					WorkBuffer.Reset();

					FPseudoConstantQ::FillArrayWithConstantQBand(CQTBandSettings, Weights, StartIndex);

					if (Weights.Num() > 0)
					{
						WorkBuffer.AddUninitialized(Weights.Num());
					}
				}
			};


		public:
			FSpectrumBandExtractor(const FSpectrumBandExtractorSettings& InSettings)
			:	Settings(InSettings)
			{
			}

			virtual void SetSettings(const FSpectrumBandExtractorSettings& InSettings) override
			{
				bool bSettingsChanged = (Settings != InSettings);
				Settings = InSettings;

				if (bSettingsChanged)
				{

					// If the settings have changed from the previous call, the band specs
					// need to be updated with the new information.
					UpdateBandSpecs();
				}
			}
			
			// Clear out all added bands.
			virtual void RemoveAllBands() override
			{
				NNBandSpecs.Reset();
				LerpBandSpecs.Reset();
				QuadraticBandSpecs.Reset();
				CQTBandSpecs.Reset();
			}

			// Return total number of bands.
			virtual int32 GetNumBands() const override
			{
				int32 Num = NNBandSpecs.Num();
				Num += LerpBandSpecs.Num();
				Num += QuadraticBandSpecs.Num();
				Num += CQTBandSpecs.Num();

				return Num;
			}

			// Add a nearest neighbor band
			virtual void AddNearestNeighborBand(float InCenterFrequency, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize) override
			{
				AddBand<FNNBandSpec>(NNBandSpecs, InCenterFrequency, InMetric, InDecibelNoiseFloor, bInDoNormalize);
			}

			// Add a linear interpolated band.
			virtual void AddLerpBand(float InCenterFrequency, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize) override
			{
				AddBand<FLerpBandSpec>(LerpBandSpecs, InCenterFrequency, InMetric, InDecibelNoiseFloor, bInDoNormalize);
			}

			// Add a quadratic interplated band.
			virtual void AddQuadraticBand(float InCenterFrequency, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize) override
			{
				AddBand<FQuadraticBandSpec>(QuadraticBandSpecs, InCenterFrequency, InMetric, InDecibelNoiseFloor, bInDoNormalize);
			}

			// Add a constant Q band
			virtual void AddConstantQBand(float InCenterFrequency, float InQFactor, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize) override
			{
				FCQTBandSpec& Band = AddBand<FCQTBandSpec>(CQTBandSpecs, InCenterFrequency, InMetric, InDecibelNoiseFloor, bInDoNormalize);
				Band.QFactor = FMath::Clamp(InQFactor, 0.001f, 100.f);
			}

			// Extract band from input.
			virtual void ExtractBands(const AlignedFloatBuffer& InComplexBuffer, TArray<float>& OutValues) override
			{
				const int32 NumComplex = InComplexBuffer.Num();

				check(NumComplex == (Settings.FFTSize + 2));

				OutValues.Reset();
				OutValues.AddZeroed(GetNumBands());

				PowerSpectrum.Reset();
				if (NumComplex > 1)
				{
					PowerSpectrum.AddUninitialized(NumComplex / 2);
				}

				// All band extractors expect a power spectrum
				ArrayComplexToPower(InComplexBuffer, PowerSpectrum);

				ExtractBands(PowerSpectrum, NNBandSpecs, OutValues);
				ExtractBands(PowerSpectrum, LerpBandSpecs, OutValues);
				ExtractBands(PowerSpectrum, QuadraticBandSpecs, OutValues);
				ExtractBands(PowerSpectrum, CQTBandSpecs, OutValues);
			}

		private:

			// Adds a band spec and returns a reference to the added spec.
			template<typename T> 
			T& AddBand(TArray<T>& InBandSpecs, float InCenterFrequency, EMetric InMetric, float InDecibelNoiseFloor, bool bInDoNormalize)
			{
				int32 OutIndex = GetNumBands();

				T BandSpec(Settings, OutIndex, InCenterFrequency, InMetric, InDecibelNoiseFloor, bInDoNormalize);

				return InBandSpecs.Add_GetRef(BandSpec);
			}

			// Calls update on all band specs in the array.
			template<typename T>
			void UpdateBandSpecs(TArray<T>& InSpecs)
			{
				for (T& BandSpec : InSpecs)
				{
					BandSpec.Update(Settings);
				}
			}

			// Updates all band specs.
			void UpdateBandSpecs()
			{
				UpdateBandSpecs<FNNBandSpec>(NNBandSpecs);
				UpdateBandSpecs<FLerpBandSpec>(LerpBandSpecs);
				UpdateBandSpecs<FQuadraticBandSpec>(QuadraticBandSpecs);
				UpdateBandSpecs<FCQTBandSpec>(CQTBandSpecs);
			}


			// Extract nearest neighbor bands
			void ExtractBands(const AlignedFloatBuffer& InPowerSpectrum, const TArray<FNNBandSpec>& InNNBandSpecs, TArray<float>& OutValues) const
			{
				float* OutData = OutValues.GetData();
				const float* InData = InPowerSpectrum.GetData();
				int32 InNum = InPowerSpectrum.Num();

				for (const FNNBandSpec& Spec : InNNBandSpecs)
				{
					check(Spec.OutIndex >= 0);
					check(Spec.OutIndex < OutValues.Num());
					check(Spec.Index < InNum);
					check(Spec.Index >= 0);
					OutData[Spec.OutIndex] = ApplyScaleAndMetric(Spec, InData[Spec.Index]);
				};
			}

			// Extract linearly interpolated bands
			void ExtractBands(const AlignedFloatBuffer& InPowerSpectrum, const TArray<FLerpBandSpec>& InLerpBandSpecs, TArray<float>& OutValues) const
			{
				float* OutData = OutValues.GetData();
				const float* InData = InPowerSpectrum.GetData();
				int32 InNum = InPowerSpectrum.Num();

				for (const FLerpBandSpec& Spec : InLerpBandSpecs)
				{
					check(Spec.OutIndex >= 0);
					check(Spec.OutIndex < OutValues.Num());
					check(Spec.LowerIndex < InNum);
					check(Spec.LowerIndex >= 0);
					check(Spec.UpperIndex < InNum);
					check(Spec.UpperIndex >= 0);

					float Value = FMath::Lerp<float>(InData[Spec.LowerIndex], InData[Spec.UpperIndex], Spec.Alpha);
					OutData[Spec.OutIndex] = ApplyScaleAndMetric(Spec, Value);
				};
			}

			// Extract quadratically interpolated bands.
			void ExtractBands(const AlignedFloatBuffer& InPowerSpectrum, const TArray<FQuadraticBandSpec>& InQuadraticBandSpecs, TArray<float>& OutValues) const
			{
				float* OutData = OutValues.GetData();
				const float* InData = InPowerSpectrum.GetData();
				int32 InNum = InPowerSpectrum.Num();

				for (const FQuadraticBandSpec& Spec : InQuadraticBandSpecs)
				{
					check(Spec.OutIndex >= 0);
					check(Spec.OutIndex < OutValues.Num());
					check(Spec.LowerIndex < InNum);
					check(Spec.LowerIndex >= 0);
					check(Spec.MidIndex < InNum);
					check(Spec.MidIndex >= 0);
					check(Spec.UpperIndex < InNum);
					check(Spec.UpperIndex >= 0);

					const float LowerValue = InData[Spec.LowerIndex];
					const float MidValue = InData[Spec.MidIndex];
					const float UpperValue = InData[Spec.UpperIndex];
					
					float Value = (LowerValue * Spec.LowerWeight) + (MidValue * Spec.MidWeight) + (UpperValue * Spec.UpperWeight);

					OutData[Spec.OutIndex] = ApplyScaleAndMetric(Spec, Value);
				}
			}

			// Extract constant q bands.
			void ExtractBands(const AlignedFloatBuffer& InPowerSpectrum, const TArray<FCQTBandSpec>& InCQTBandSpecs, TArray<float>& OutValues) const
			{
				float* OutData = OutValues.GetData();
				const float* InData = InPowerSpectrum.GetData();
				int32 InNum = InPowerSpectrum.Num();

				for (const FCQTBandSpec& Spec : InCQTBandSpecs)
				{
					check(Spec.OutIndex >= 0);
					check(Spec.OutIndex < OutValues.Num());
					check(Spec.StartIndex < InNum);
					check(Spec.StartIndex >= 0);
					check((Spec.StartIndex + Spec.Weights.Num()) <= InNum);

					float Value = 0.f;
					int32 NumWeights = Spec.Weights.Num();

					if (NumWeights > 0)
					{
						check(NumWeights == Spec.WorkBuffer.Num());
						FMemory::Memcpy(Spec.WorkBuffer.GetData(), &InData[Spec.StartIndex], NumWeights * sizeof(float));

						ArrayMultiplyInPlace(Spec.Weights, Spec.WorkBuffer);
						ArraySum(Spec.WorkBuffer, Value);
					}

					OutData[Spec.OutIndex] = ApplyScaleAndMetric(Spec, Value);
				}
				
			}

			// Scale and apply metric to a band value
			float ApplyScaleAndMetric(const FBandSpec& InBandSpec, float InValue) const
			{
				float OutValue = InValue * InBandSpec.PowerSpectrumScale;

				switch (InBandSpec.Metric)
				{
					case ISpectrumBandExtractor::EMetric::Magnitude:
						OutValue = FMath::Sqrt(OutValue);
						break;

					case ISpectrumBandExtractor::EMetric::Decibel:
						OutValue = 10.f * FMath::Loge(OutValue) / SpectrumAnalyzerIntrinsics::Loge10;
						if (!FMath::IsFinite(OutValue) || (OutValue < InBandSpec.DbNoiseFloor))
						{
							OutValue = InBandSpec.DbNoiseFloor;
						}

						if (InBandSpec.bDoNormalize)
						{
							OutValue -= InBandSpec.DbNoiseFloor;
							if (InBandSpec.DbNoiseFloor < 0.f)
							{
								OutValue /= (-InBandSpec.DbNoiseFloor);
							}
						}
						break;

					case ISpectrumBandExtractor::EMetric::Power:
					default:
						OutValue = OutValue;
						break;
				}

				if (InBandSpec.bDoNormalize)
				{
					OutValue = FMath::Clamp(OutValue, 0.f, 1.f);
				}

				return OutValue;
			}

			FSpectrumBandExtractorSettings Settings;

			AlignedFloatBuffer PowerSpectrum;
			TArray<FNNBandSpec> NNBandSpecs;
			TArray<FLerpBandSpec> LerpBandSpecs;
			TArray<FQuadraticBandSpec> QuadraticBandSpecs;
			TArray<FCQTBandSpec> CQTBandSpecs;
	};


	// Creates a concreted implementation of teh ISpectrumBandExtractor interface.
	TUniquePtr<ISpectrumBandExtractor> ISpectrumBandExtractor::CreateSpectrumBandExtractor(const FSpectrumBandExtractorSettings& InSettings)
	{
		return MakeUnique<FSpectrumBandExtractor>(InSettings);
	}

	FSpectrumAnalyzer::FSpectrumAnalyzer()
		: CurrentSettings(FSpectrumAnalyzerSettings())
		, bSettingsWereUpdated(false)
		, bIsInitialized(false)
		, SampleRate(0.0f)
		, Window(CurrentSettings.WindowType, (int32)CurrentSettings.FFTSize, 1, false)
		, InputQueue(FMath::Max((int32)CurrentSettings.FFTSize * 4, 4096))
		, FrequencyBuffer((int32)CurrentSettings.FFTSize)
		, LockedFrequencyVector(nullptr)
	{
	}

	FSpectrumAnalyzer::FSpectrumAnalyzer(const FSpectrumAnalyzerSettings& InSettings, float InSampleRate)
		: CurrentSettings(InSettings)
		, bSettingsWereUpdated(false)
		, bIsInitialized(true)
		, SampleRate(InSampleRate)
		, Window(InSettings.WindowType, (int32)InSettings.FFTSize, 1, false)
		, InputQueue(FMath::Max((int32)CurrentSettings.FFTSize * 4, 4096))
		, FrequencyBuffer((int32)InSettings.FFTSize)
		, LockedFrequencyVector(nullptr)
	{
		ResetSettings();
	}

	FSpectrumAnalyzer::FSpectrumAnalyzer(float InSampleRate)
		: CurrentSettings(FSpectrumAnalyzerSettings())
		, bSettingsWereUpdated(false)
		, bIsInitialized(true)
		, SampleRate(InSampleRate)
		, Window(CurrentSettings.WindowType, (int32)CurrentSettings.FFTSize, 1, false)
		, InputQueue(FMath::Max((int32)CurrentSettings.FFTSize * 4, 4096))
		, FrequencyBuffer((int32)CurrentSettings.FFTSize)
		, LockedFrequencyVector(nullptr)
	{
		ResetSettings();
	}

	FSpectrumAnalyzer::~FSpectrumAnalyzer()
	{
		if (AsyncAnalysisTask.IsValid())
		{
			AsyncAnalysisTask->EnsureCompletion(false);
		}
	}

	void FSpectrumAnalyzer::Init(float InSampleRate)
	{
		FSpectrumAnalyzerSettings DefaultSettings = FSpectrumAnalyzerSettings();
		Init(DefaultSettings, InSampleRate);
	}

	void FSpectrumAnalyzer::Init(const FSpectrumAnalyzerSettings& InSettings, float InSampleRate)
	{
		CurrentSettings = InSettings;
		bSettingsWereUpdated = false;
		SampleRate = InSampleRate;
		InputQueue.SetCapacity(FMath::Max((int32)CurrentSettings.FFTSize * 4, 4096));
		FrequencyBuffer.Reset((int32)CurrentSettings.FFTSize);
		ResetSettings();

		bIsInitialized = true;
	}

	void FSpectrumAnalyzer::ResetSettings()
	{
		// If the game thread has locked a frequency vector, we can't resize our buffers under it.
		// Thus, wait until it's unlocked.
		if (LockedFrequencyVector != nullptr)
		{
			return;
		}

		Window = FWindow(CurrentSettings.WindowType, (int32)CurrentSettings.FFTSize, 1, false);
		FFTSize = (int32) CurrentSettings.FFTSize;
		int32 Log2FFTSize = 9;
		if (FFTSize > 0)
		{
			// FFTSize must be log2
			check(FMath::CountBits(FFTSize) == 1);
			Log2FFTSize = FMath::CountTrailingZeros(FFTSize);
		}

		AnalysisTimeDomainBuffer.Reset();
		
		if (FMath::IsNearlyZero(CurrentSettings.HopSize))
		{
			HopInSamples = GetCOLAHopSizeForWindow(CurrentSettings.WindowType, (uint32)CurrentSettings.FFTSize);
		}
		else
		{
			HopInSamples = FMath::FloorToInt((float)CurrentSettings.FFTSize * CurrentSettings.HopSize);
		}

		// Create a new FFT
		FFFTSettings FFTSettings;
		FFTSettings.Log2Size = Log2FFTSize;
		FFTSettings.bArrays128BitAligned = true;
		FFTSettings.bEnableHardwareAcceleration = true;

		FFT = FFFTFactory::NewFFTAlgorithm(FFTSettings);

		if (!FFT.IsValid())
		{
			if (FFFTFactory::AreFFTSettingsSupported(FFTSettings))
			{
				UE_LOG(LogSignalProcessing, Error, TEXT("Failed to create fft for supported settings."))
			}
			else
			{
				UE_LOG(LogSignalProcessing, Warning, TEXT("FFT Settings are unsupported."))
			}
			FFTScaling = EFFTScaling::None;

			if (FFTSize > 0)
			{
				AnalysisTimeDomainBuffer.AddZeroed(FFTSize);
				FrequencyBuffer.Reset(FFTSize);
			}
		}
		else
		{
			int32 NumFFTInput = FFT->NumInputFloats();
			int32 NumFFTOutput = FFT->NumOutputFloats();
			FFTScaling = FFT->ForwardScaling();

			if (NumFFTInput > 0)
			{
				AnalysisTimeDomainBuffer.AddUninitialized(NumFFTInput);
			}

			FrequencyBuffer.Reset(NumFFTOutput);
		}
		
		bSettingsWereUpdated = false;
	}

	void FSpectrumAnalyzer::PerformInterpolation(const AlignedFloatBuffer& InComplexBuffer, FSpectrumAnalyzerSettings::EPeakInterpolationMethod InMethod, const float InFreq, float& OutReal, float& OutImag)
	{
		const float* InComplexData = InComplexBuffer.GetData();
		const int32 VectorLength = InComplexBuffer.Num();
		const int32 NyquistPosition = VectorLength - 2;
		
		const float Nyquist = SampleRate / 2.f;

		// Fractional position in the frequency vector in terms of indices.
		// float Position = NyquistPosition + (InFreq / Nyquist);
		const float NormalizedFreq = (InFreq / Nyquist);
		float Position = InFreq >= 0 ? (NormalizedFreq * VectorLength) : 0.f;
		
		switch (InMethod)
		{
		case Audio::FSpectrumAnalyzerSettings::EPeakInterpolationMethod::NearestNeighbor:
		{
			int32 Index = FMath::RoundToInt(Position) & SpectrumAnalyzerIntrinsics::EvenNumberMask;

			Index = FMath::Clamp(Index, 0, NyquistPosition);

			OutReal = InComplexData[Index];
			OutImag = InComplexData[Index + 1];

			break;
		}
			
		case Audio::FSpectrumAnalyzerSettings::EPeakInterpolationMethod::Linear:
		{
			int32 LowerIndex = FMath::FloorToInt(Position) & SpectrumAnalyzerIntrinsics::EvenNumberMask;
			int32 UpperIndex = LowerIndex + 2;

			LowerIndex = FMath::Clamp(LowerIndex, 0, NyquistPosition);
			UpperIndex = FMath::Clamp(UpperIndex, 0, NyquistPosition);

			const float PositionFraction = Position - LowerIndex;

			const float y1Real = InComplexData[LowerIndex];
			const float y2Real = InComplexData[UpperIndex];

			OutReal = FMath::Lerp<float>(y1Real, y1Real, PositionFraction);

			const float y1Imag = InComplexData[LowerIndex + 1];
			const float y2Imag = InComplexData[UpperIndex + 1];

			OutImag = FMath::Lerp<float>(y1Imag, y2Imag, PositionFraction);
			break;
		}	
		case Audio::FSpectrumAnalyzerSettings::EPeakInterpolationMethod::Quadratic:
		{
			// Note: math here does not interpolate quadratically. 
			const int32 MidIndex = FMath::Clamp(FMath::RoundToInt(Position) & SpectrumAnalyzerIntrinsics::EvenNumberMask, 0, NyquistPosition);
			const int32 LowerIndex = FMath::Max(0, MidIndex - 2);
			const int32 UpperIndex = FMath::Min(NyquistPosition, MidIndex + 2);

			const float y1Real = InComplexData[LowerIndex];
			const float y2Real = InComplexData[MidIndex];
			const float y3Real = InComplexData[UpperIndex];

			const float InterpReal = (y3Real - y1Real) / (2.f * (2.f * y2Real - y1Real - y3Real));
			
			OutReal = InterpReal;

			const float y1Imag = InComplexData[LowerIndex + 1];
			const float y2Imag = InComplexData[MidIndex + 1];
			const float y3Imag = InComplexData[UpperIndex + 1];
			const float InterpImag = (y3Imag - y1Imag) / (2.f * (2.f * y2Imag - y1Imag - y3Imag));

			OutImag = InterpImag;
			break;
		}
			
		default:
			break;
		}
	}

	void FSpectrumAnalyzer::SetSettings(const FSpectrumAnalyzerSettings& InSettings)
	{
		CurrentSettings = InSettings;
		bSettingsWereUpdated = true;
	}

	void FSpectrumAnalyzer::GetSettings(FSpectrumAnalyzerSettings& OutSettings)
	{
		OutSettings = CurrentSettings;
	}

	float FSpectrumAnalyzer::GetMagnitudeForFrequency(float InFrequency)
	{
		if (!bIsInitialized)
		{
			return 0.f;
		}

		const AlignedFloatBuffer* OutVector = nullptr;
		bool bShouldUnlockBuffer = true;

		if (LockedFrequencyVector)
		{
			OutVector = LockedFrequencyVector;
			bShouldUnlockBuffer = false;
		}
		else
		{
			OutVector = &FrequencyBuffer.LockMostRecentBuffer();
		}

		// Perform work.
		if (OutVector)
		{
			float OutMagnitude = 0.0f;

			float InterpolatedReal, InterpolatedImag;
			PerformInterpolation(*OutVector, CurrentSettings.InterpolationMethod, InFrequency, InterpolatedReal, InterpolatedImag);

			OutMagnitude = FMath::Sqrt((InterpolatedReal * InterpolatedReal) + (InterpolatedImag * InterpolatedImag));

			if (bShouldUnlockBuffer)
			{
				FrequencyBuffer.UnlockBuffer();
			}

			return OutMagnitude;
		}

		// If we got here, something went wrong, so just output zero.
		return 0.0f;
	}

	float FSpectrumAnalyzer::GetPhaseForFrequency(float InFrequency)
	{
		if (!bIsInitialized)
		{
			return 0.f;
		}

		const AlignedFloatBuffer* OutVector = nullptr;
		bool bShouldUnlockBuffer = true;

		if (LockedFrequencyVector)
		{
			OutVector = LockedFrequencyVector;
			bShouldUnlockBuffer = false;
		}
		else
		{
			OutVector = &FrequencyBuffer.LockMostRecentBuffer();
		}

		// Perform work.
		if (OutVector)
		{
			float OutPhase = 0.0f;

			float InterpolatedReal, InterpolatedImag;
			PerformInterpolation(*OutVector, CurrentSettings.InterpolationMethod, InFrequency, InterpolatedReal, InterpolatedImag);

			OutPhase = FMath::Atan2(InterpolatedImag, InterpolatedReal);

			if (bShouldUnlockBuffer)
			{
				FrequencyBuffer.UnlockBuffer();
			}

			return OutPhase;
		}

		// If we got here, something went wrong, so just output zero.
		return 0.0f;
	}

	// Return bands extracted by band extractor.
	void FSpectrumAnalyzer::GetBands(ISpectrumBandExtractor& InExtractor, TArray<float>& OutValues) 
	{
		OutValues.Reset();

		if (!bIsInitialized)
		{
			return;
		}

		const AlignedFloatBuffer* OutVector = nullptr;
		bool bShouldUnlockBuffer = true;

		FSpectrumBandExtractorSettings ExtractorSettings;
		ExtractorSettings.SampleRate = SampleRate;
		ExtractorSettings.FFTSize = FFTSize;
		ExtractorSettings.FFTScaling = FFTScaling;
		ExtractorSettings.WindowType = Window.GetWindowType();

		// This should have minimal cost if settings have not changed between calls.
		InExtractor.SetSettings(ExtractorSettings);

		if (LockedFrequencyVector)
		{
			OutVector = LockedFrequencyVector;
			bShouldUnlockBuffer = false;
		}
		else
		{
			OutVector = &FrequencyBuffer.LockMostRecentBuffer();
		}

		// Perform work.
		if (OutVector)
		{
			InExtractor.ExtractBands(*OutVector, OutValues);

			if (bShouldUnlockBuffer)
			{
				FrequencyBuffer.UnlockBuffer();
			}
		}
	}

	void FSpectrumAnalyzer::LockOutputBuffer()
	{
		if (!bIsInitialized)
		{
			return;
		}

		if (LockedFrequencyVector != nullptr)
		{
			FrequencyBuffer.UnlockBuffer();
		}

		LockedFrequencyVector = &FrequencyBuffer.LockMostRecentBuffer();
	}

	void FSpectrumAnalyzer::UnlockOutputBuffer()
	{
		if (!bIsInitialized)
		{
			return;
		}

		if (LockedFrequencyVector != nullptr)
		{
			FrequencyBuffer.UnlockBuffer();
			LockedFrequencyVector = nullptr;
		}
	}

	bool FSpectrumAnalyzer::PushAudio(const TSampleBuffer<float>& InBuffer)
	{
		check(InBuffer.GetNumChannels() == 1);
		return PushAudio(InBuffer.GetData(), InBuffer.GetNumSamples());
	}

	bool FSpectrumAnalyzer::PushAudio(const float* InBuffer, int32 NumSamples)
	{
		return InputQueue.Push(InBuffer, NumSamples) == NumSamples;
	}

	bool FSpectrumAnalyzer::PerformAnalysisIfPossible(bool bUseLatestAudio, bool bAsync)
	{
		if (!bIsInitialized)
		{
			return false;
		}		

		if (bAsync)
		{
			// if bAsync is true, kick off a new task if one isn't in flight already, and return.
			if (!AsyncAnalysisTask.IsValid())
			{
				AsyncAnalysisTask.Reset(new FSpectrumAnalyzerTask(this, bUseLatestAudio));
				AsyncAnalysisTask->StartBackgroundTask();
			}
			else if (AsyncAnalysisTask->IsDone())
			{
				AsyncAnalysisTask->StartBackgroundTask();
			}

			return true;
		}

		// If settings were updated, perform resizing and parameter updates here:
		if (bSettingsWereUpdated)
		{
			ResetSettings();
		}

		AlignedFloatBuffer& FFTOutput = FrequencyBuffer.StartWorkOnBuffer();

		// If we have enough audio pushed to the spectrum analyzer and we have an available buffer to work in,
		// we can start analyzing.
		if (InputQueue.Num() >= ((uint32)FFTSize))
		{
			float* TimeDomainBuffer = AnalysisTimeDomainBuffer.GetData();

			if (bUseLatestAudio)
			{
				// If we are only using the latest audio, scrap the oldest audio in the InputQueue:
				InputQueue.SetNum((uint32)FFTSize);
			}

			// Perform pop/peek here based on FFT size and hop amount.
			const int32 PeekAmount = FFTSize - HopInSamples;
			InputQueue.Pop(TimeDomainBuffer, HopInSamples);
			InputQueue.Peek(TimeDomainBuffer + HopInSamples, PeekAmount);

			// apply window if necessary.
			Window.ApplyToBuffer(TimeDomainBuffer);

			// Perform FFT.
			if (FFT.IsValid())
			{
				check(AnalysisTimeDomainBuffer.Num() == FFT->NumInputFloats());
				check(FFTOutput.Num() == FFT->NumOutputFloats());

				FFT->ForwardRealToComplex(TimeDomainBuffer, FFTOutput.GetData());
			}
			else
			{
				if (FFTOutput.Num() > 0)
				{
					FMemory::Memset(FFTOutput.GetData(), 0, sizeof(float) * FFTOutput.Num());
				}
			}

			// We're done, so unlock this vector.
			FrequencyBuffer.StopWorkOnBuffer();

			return true;
		}
		else
		{
			return false;
		}
	}

	bool FSpectrumAnalyzer::IsInitialized()
	{
		return bIsInitialized;
	}

	static const int32 SpectrumAnalyzerBufferSize = 4;

	FSpectrumAnalyzerBuffer::FSpectrumAnalyzerBuffer()
		: OutputIndex(0)
		, InputIndex(0)
	{
	}

	FSpectrumAnalyzerBuffer::FSpectrumAnalyzerBuffer(int32 InNum)
	{
		Reset(InNum);
	}

	void FSpectrumAnalyzerBuffer::Reset(int32 InNum)
	{
		FScopeLock ScopeLock(&BufferIndicesCriticalSection);

		static_assert(SpectrumAnalyzerBufferSize > 2, "Please ensure that SpectrumAnalyzerBufferSize is greater than 2.");
		
		ComplexBuffers.Reset();

		for (int32 Index = 0; Index < SpectrumAnalyzerBufferSize; Index++)
		{
			AlignedFloatBuffer& Buffer = ComplexBuffers.Emplace_GetRef();

			if (InNum > 0)
			{
				Buffer.AddZeroed(InNum);
			}
		}

		InputIndex = 0;
		OutputIndex = 0;
	}

	void FSpectrumAnalyzerBuffer::IncrementInputIndex()
	{
		FScopeLock ScopeLock(&BufferIndicesCriticalSection);

		InputIndex = (InputIndex + 1) % SpectrumAnalyzerBufferSize;
		if (InputIndex == OutputIndex)
		{
			InputIndex = (InputIndex + 1) % SpectrumAnalyzerBufferSize;
		}

		check(InputIndex != OutputIndex);
	}

	void FSpectrumAnalyzerBuffer::IncrementOutputIndex()
	{
		FScopeLock ScopeLock(&BufferIndicesCriticalSection);

		OutputIndex = (OutputIndex + 1) % SpectrumAnalyzerBufferSize;
		if (InputIndex == OutputIndex)
		{
			OutputIndex = (OutputIndex + 1) % SpectrumAnalyzerBufferSize;
		}

		check(InputIndex != OutputIndex);
	}

	AlignedFloatBuffer& FSpectrumAnalyzerBuffer::StartWorkOnBuffer()
	{
		return ComplexBuffers[InputIndex];
	}

	void FSpectrumAnalyzerBuffer::StopWorkOnBuffer()
	{
		IncrementInputIndex();
	}

	const AlignedFloatBuffer& FSpectrumAnalyzerBuffer::LockMostRecentBuffer() const
	{
		return ComplexBuffers[OutputIndex];
	}

	void FSpectrumAnalyzerBuffer::UnlockBuffer()
	{
		IncrementOutputIndex();
	}


	void FSpectrumAnalysisAsyncWorker::DoWork()
	{
		Analyzer->PerformAnalysisIfPossible(bUseLatestAudio, false);
	}
}

