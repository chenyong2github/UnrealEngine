// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableImporter.h"

#include "DSP/FloatArrayMath.h"
#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "WaveTableSampler.h"
#include "DSP/EnvelopeFollower.h"


namespace WaveTable
{
	namespace ImporterPrivate
	{
		UE_NODISCARD static FORCEINLINE int32 RatioToIndex(float InRatio, int32 InArrayNum)
		{
			check(InArrayNum > 0);
			check(InRatio >= 0.0f);

			return FMath::Min(InArrayNum - 1, FMath::TruncToInt32(InRatio * InArrayNum));
		};
	}

	FImporter::FImporter(const FWaveTableSettings& InSettings, EWaveTableResolution InResolution, bool bInBipolar)
		: Settings(InSettings)
		, Resolution(InResolution)
		, bBipolar(bInBipolar)
	{
		Sampler.SetInterpolationMode(FWaveTableSampler::EInterpolationMode::Cubic);
		Sampler.SetPhase(Settings.Phase);
	}

	void FImporter::Process(TArray<float>& OutWaveTable)
	{
		using namespace ImporterPrivate;

		const int32 ResolutionInt = ResolutionToInt32(Resolution);

		OutWaveTable.Reset();
		OutWaveTable.AddZeroed(ResolutionInt);

		if (Resolution == EWaveTableResolution::None)
		{
			return;
		}

		int32 SourceNumSamples = Settings.SourcePCMData.Num();
		if (SourceNumSamples == 0)
		{
			return;
		}

		// 1. Use Top & Tail to determine start offset & sample count of SourceArrayView
		int32 SourceTopOffset = 0;
		if (Settings.Top > 0.0f)
		{
			SourceTopOffset = ImporterPrivate::RatioToIndex(Settings.Top, SourceNumSamples);
			SourceNumSamples -= SourceTopOffset;
		}

		if (Settings.Tail > 0.0f)
		{
			const int32 SourceTailOffset = RatioToIndex(Settings.Tail, SourceNumSamples);
			SourceNumSamples -= SourceTailOffset;
		}

		if (SourceNumSamples > 0 && SourceTopOffset < Settings.SourcePCMData.Num() && !OutWaveTable.IsEmpty())
		{
			TArrayView<const float> SourceArrayView(Settings.SourcePCMData.GetData() + SourceTopOffset, SourceNumSamples);

			// 2. Resample into table
			Sampler.Reset();
			Sampler.Process(SourceArrayView, OutWaveTable);

			// 3. Apply offset
			float TableOffset = 0.0f;
			if (Settings.bRemoveOffset || !bBipolar)
			{
				TableOffset = Audio::ArrayGetAverageValue(OutWaveTable);
				Audio::ArrayAddConstantInplace(OutWaveTable, -1.0f * TableOffset);
			}

			// 4. Normalize
			if (Settings.bNormalize)
			{
				const float MaxValue = Audio::ArrayMaxAbsValue(OutWaveTable);
				if (MaxValue > 0.0f)
				{
					Audio::ArrayMultiplyByConstantInPlace(OutWaveTable, 1.0f / MaxValue);
				}
			}

			// 6. Apply fades
			if (Settings.FadeIn > 0.0f)
			{
				const int32 FadeLength = RatioToIndex(Settings.FadeIn, OutWaveTable.Num());
				TArrayView<float> FadeView(OutWaveTable.GetData(), FadeLength);
				Audio::ArrayFade(FadeView, 0.0f, 1.0f);
			}

			if (Settings.FadeOut > 0.0f)
			{
				const int32 FadeLastIndex = RatioToIndex(Settings.FadeOut, OutWaveTable.Num());
				const int32 FadeInitIndex = RatioToIndex(1.0f - Settings.FadeOut, OutWaveTable.Num());
				TArrayView<float> FadeView = TArrayView<float>(&OutWaveTable[FadeInitIndex], FadeLastIndex + 1);
				Audio::ArrayFade(FadeView, 1.0f, 0.0f);
			}

			// 7. Finalize if unipolar source
			if (!bBipolar)
			{
				Audio::ArrayAbsInPlace(OutWaveTable);

				// If not requesting offset removal, add back (is always removed above for
				// unipolar to ensure fades are centered around offset)
				if (!Settings.bRemoveOffset)
				{
					Audio::ArrayAddConstantInplace(OutWaveTable, TableOffset);
				}
			}
		}
	} // namespace Importer
} // namespace WaveTable
