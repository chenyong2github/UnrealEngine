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

	FImporter::FImporter(const FWaveTableSettings& InOptions, EWaveTableResolution InResolution)
		: Options(InOptions)
		, Resolution(InResolution)
	{
		Sampler.SetInterpolationMode(FWaveTableSampler::EInterpolationMode::Cubic);
		Sampler.SetPhase(Options.Phase);
	}

	void FImporter::Process(TArray<float>& OutWaveTable)
	{
		using namespace ImporterPrivate;

		OutWaveTable.Reset();
		OutWaveTable.AddZeroed(ResolutionToInt32(Resolution));

		if (Resolution == EWaveTableResolution::None)
		{
			return;
		}

		int32 SourceNumSamples = Options.SourcePCMData.Num();
		if (SourceNumSamples == 0)
		{
			return;
		}

		// 1. Use Top & Tail to determine start offset & sample count of SourceArrayView
		int32 SourceTopOffset = 0;
		if (Options.Top > 0.0f)
		{
			SourceTopOffset = ImporterPrivate::RatioToIndex(Options.Top, SourceNumSamples);
			SourceNumSamples -= SourceTopOffset;
		}

		if (Options.Tail > 0.0f)
		{
			const int32 SourceTailOffset = RatioToIndex(Options.Tail, SourceNumSamples);
			SourceNumSamples -= SourceTailOffset;
		}

		if (SourceNumSamples > 0 && SourceTopOffset < Options.SourcePCMData.Num() && !OutWaveTable.IsEmpty())
		{
			TArrayView<const float> SourceArrayView(Options.SourcePCMData.GetData() + SourceTopOffset, SourceNumSamples);
			TArrayView<float> WaveTableView(OutWaveTable);
			Sampler.Process(SourceArrayView, WaveTableView);

			if (Options.FadeIn > 0.0f)
			{
				const int32 FadeLength = RatioToIndex(Options.FadeIn, OutWaveTable.Num());
				TArrayView<float> FadeView(OutWaveTable.GetData(), FadeLength);
				Audio::ArrayFade(FadeView, 0.0f, 1.0f);
			}

			if (Options.FadeOut > 0.0f)
			{
				const int32 FadeLastIndex = RatioToIndex(Options.FadeOut, OutWaveTable.Num());
				const int32 FadeInitIndex = RatioToIndex(1.0f - Options.FadeOut, OutWaveTable.Num());
				TArrayView<float> FadeView = TArrayView<float>(&OutWaveTable[FadeInitIndex], FadeLastIndex + 1);
				Audio::ArrayFade(FadeView, 1.0f, 0.0f);
			}

			if (Options.bRemoveOffset)
			{
				const float TableOffset = Audio::ArrayGetAverageValue(OutWaveTable);
				Audio::ArrayAddConstantInplace(OutWaveTable, -1.0f * TableOffset);
			}

			if (Options.bConvertToEnvelope)
			{
				Audio::ArrayAbsInPlace(OutWaveTable);
			}
			else
			{
				Audio::ArrayAddConstantInplace(OutWaveTable, 1.0f);
				Audio::ArrayMultiplyByConstantInPlace(OutWaveTable, 0.5f);
			}

			if (Options.bNormalize)
			{
				const float MaxValue = Audio::ArrayMaxAbsValue(OutWaveTable);
				if (MaxValue > 0.0f)
				{
					Audio::ArrayMultiplyByConstantInPlace(OutWaveTable, 1.0f / MaxValue);
				}
			}
		}
	} // namespace Importer
} // namespace WaveTable
