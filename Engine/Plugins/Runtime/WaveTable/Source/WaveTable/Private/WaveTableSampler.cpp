// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableSampler.h"

#include "DSP/Dsp.h"
#include "DSP/FloatArrayMath.h"


namespace WaveTable
{
	namespace UtilitiesPrivate
	{
		UE_NODISCARD FORCEINLINE FVector2f GetTangentP0(const float* InTableView, const float NextValue, float InIndex, int32 InArraySize)
		{
			const int32 Index = FMath::TruncToInt32(InIndex);
			const float LastValue = InTableView[(Index - 1) % InArraySize];
			return FVector2f(2.0f, NextValue - LastValue).GetSafeNormal();
		};

		UE_NODISCARD FORCEINLINE FVector2f GetTangentP1(const float* InTableView, const float LastValue, float InIndex, int32 InArraySize)
		{
			const int32 Index = FMath::TruncToInt32(InIndex);
			const float NextValue = InTableView[(Index + 1) % InArraySize];
			return FVector2f(2.0f, NextValue - LastValue).GetSafeNormal();
		};

		using FInlineInterpolator = TFunctionRef<void(TArrayView<const float> InTableView, TArrayView<float> OutSamplesView)>;

		auto CubicIndexInterpolator = [](TArrayView<const float> InTableView, TArrayView<float> InOutIndicesToValues)
		{
			const int32 NumTableSamples = InTableView.Num();
			const float* InTable = InTableView.GetData();
			for (int32 i = 0; i < InOutIndicesToValues.Num(); ++i)
			{
				float& IndexToOutput = InOutIndicesToValues[i];
				const int32 LastIndexInt = FMath::TruncToInt32(IndexToOutput);

				const FVector2f P0 = { (float)LastIndexInt, InTableView[LastIndexInt % NumTableSamples] };
				const FVector2f P1 = { (float)(LastIndexInt + 1), InTable[(LastIndexInt + 1) % NumTableSamples] };
				const FVector2f TangentP0 = GetTangentP0(InTable, P1.Y, IndexToOutput, NumTableSamples);
				const FVector2f TangentP1 = GetTangentP1(InTable, P0.Y, IndexToOutput, NumTableSamples);
				const float Alpha = IndexToOutput - LastIndexInt;

				IndexToOutput = FMath::CubicInterp(P0, TangentP0, P1, TangentP1, Alpha).Y;
			}
		};

		auto LinearIndexInterpolator = [](TArrayView<const float> InTableView, const TArrayView<float> InOutIndicesToValues)
		{
			const int32 NumTableSamples = InTableView.Num();
			const float* InTable = InTableView.GetData();
			for (int32 i = 0; i < InOutIndicesToValues.Num(); ++i)
			{
				float& IndexToOutput = InOutIndicesToValues[i];
				const int32 LastIndexInt = FMath::TruncToInt32(IndexToOutput);

				const float P0 = InTable[LastIndexInt % NumTableSamples];
				const float Alpha = IndexToOutput - LastIndexInt;
				const float P1 = InTable[(LastIndexInt + 1) % NumTableSamples];

				IndexToOutput = FMath::Lerp(P0, P1, Alpha);
			}
		};

		auto StepIndexInterpolator = [](TArrayView<const float> InTableView, const TArrayView<float> InOutIndicesToValues)
		{
			const int32 NumTableSamples = InTableView.Num();
			const float* InTable = InTableView.GetData();
			float* IndexToValue = InOutIndicesToValues.GetData();
			for (int32 i = 0; i < InOutIndicesToValues.Num(); ++i)
			{
				float& IndexToOutput = IndexToValue[i];
				const int32 LastIndexInt = FMath::TruncToInt32(IndexToOutput);
				IndexToOutput = InTable[LastIndexInt % NumTableSamples];
			}
		};
	}

	FWaveTableSampler::FWaveTableSampler()
	{
	}

	FWaveTableSampler::FWaveTableSampler(FSettings&& InSettings)
		: Settings(MoveTemp(InSettings))
	{
	}

	void FWaveTableSampler::SetFreq(float InFreq)
	{
		Settings.Freq = InFreq;
	}

	void FWaveTableSampler::SetInterpolationMode(EInterpolationMode InMode)
	{
		Settings.InterpolationMode = InMode;
	}

	void FWaveTableSampler::SetPhase(float InPhase)
	{
		Settings.Phase = InPhase;
	}

	void FWaveTableSampler::Process(TArrayView<const float> InTableView, TArrayView<float> OutSamplesView)
	{
		Process(InTableView, { }, { }, OutSamplesView);
	}

	void FWaveTableSampler::Process(TArrayView<const float> InTableView, TArrayView<const float> InFreqModulator, TArrayView<const float> InPhaseModulator, TArrayView<float> OutSamplesView)
	{
		using namespace UtilitiesPrivate;

		if (!OutSamplesView.IsEmpty())
		{
			if (InTableView.IsEmpty())
			{
				Audio::ArraySetToConstantInplace(OutSamplesView, 0.0f);
			}
			else
			{
				ComputeIndexFrequency(InTableView, InFreqModulator, OutSamplesView);
				ComputeIndexPhase(InTableView, InPhaseModulator, OutSamplesView);

				switch (Settings.InterpolationMode)
				{
					case EInterpolationMode::None:
					{
						StepIndexInterpolator(InTableView, OutSamplesView);
					}
					break;

					case EInterpolationMode::Linear:
					{
						LinearIndexInterpolator(InTableView, OutSamplesView);
					}
					break;

					case EInterpolationMode::Cubic:
					{
						CubicIndexInterpolator(InTableView, OutSamplesView);
					}
					break;

					default:
					{
						static_assert(static_cast<int32>(EInterpolationMode::COUNT) == 3, "Possible missing switch coverage for EInterpolationMode");
						checkNoEntry();
					}
					break;
				}
			}

			if (!FMath::IsNearlyZero(Settings.Offset))
			{
				Audio::ArrayAddConstantInplace(OutSamplesView, Settings.Offset);
			}

			if (!FMath::IsNearlyEqual(Settings.Amplitude, 1.0f))
			{
				Audio::ArrayMultiplyByConstantInPlace(OutSamplesView, Settings.Amplitude);
			}
		}
	}

	void FWaveTableSampler::ComputeIndexFrequency(TArrayView<const float> InTableView, TArrayView<const float> InFreqModulator, TArrayView<float> OutSamplesView) const
	{
		const float Delta = 1.0f / OutSamplesView.Num();
		if (InFreqModulator.Num() == OutSamplesView.Num())
		{
			FMemory::Memcpy(OutSamplesView.GetData(), InFreqModulator.GetData(), sizeof(float) * OutSamplesView.Num());
			Audio::ArrayMultiplyByConstantInPlace(OutSamplesView, Settings.Freq * Delta);
		}
		else
		{
			checkf(InFreqModulator.IsEmpty(), TEXT("FrequencyModulator view should be the same size as the sample view or not supplied (size of 0)."));
			Audio::ArraySetToConstantInplace(OutSamplesView, Settings.Freq * Delta);
		}

		float* OutSamples = OutSamplesView.GetData();
		for (int32 i = 0; i < OutSamplesView.Num(); ++i)
		{
			OutSamples[i] *= i;
		}
	}

	void FWaveTableSampler::ComputeIndexPhase(TArrayView<const float> InTableView, TArrayView<const float> InPhaseModulator, TArrayView<float> OutSamplesView)
	{
		const float DeltaPhase = FMath::Frac(LastIndex / InTableView.Num());
		if (InPhaseModulator.Num() == OutSamplesView.Num())
		{
			PhaseModScratch.SetNum(OutSamplesView.Num());
			Audio::ArraySetToConstantInplace(PhaseModScratch, Settings.Phase);
			Audio::ArrayAddInPlace(InPhaseModulator, PhaseModScratch);

			const float* PhaseModData = PhaseModScratch.GetData();
			float* OutSamples = OutSamplesView.GetData();
			for (int32 i = 0; i < OutSamplesView.Num(); ++i)
			{
				const float Phase = FMath::Frac(DeltaPhase + PhaseModData[i]);
				const float Offset = OutSamples[i] + Phase;
				OutSamples[i] = Offset * InTableView.Num();
			}

			LastIndex = OutSamplesView.Last() - (PhaseModScratch.Last() * InTableView.Num());
		}
		else
		{
			checkf(InPhaseModulator.IsEmpty(), TEXT("PhaseModulator view should be the same size as the sample view or not supplied (size of 0)."));

			const float Phase = FMath::Frac(DeltaPhase + Settings.Phase);
			Audio::ArrayAddConstantInplace(OutSamplesView, Phase);
			Audio::ArrayMultiplyByConstantInPlace(OutSamplesView, InTableView.Num());

			LastIndex = OutSamplesView.Last() - (Settings.Phase * InTableView.Num());
		}
	}
} // namespace WaveTable
