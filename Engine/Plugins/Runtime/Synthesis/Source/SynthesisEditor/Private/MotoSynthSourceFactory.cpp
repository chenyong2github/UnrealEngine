// Copyright Epic Games, Inc. All Rights Reserved.
#include "MotoSynthSourceFactory.h"
#include "MotoSynthSourceAsset.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "SynthesisEditorModule.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"


UClass* FAssetTypeActions_MotoSynthSource::GetSupportedClass() const
{
	return UMotoSynthSource::StaticClass();
}

const TArray<FText>& FAssetTypeActions_MotoSynthSource::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetMotoSynthSubmenu", "MotoSynth"))
	};

	return SubMenus;
}

UMotoSynthSourceFactory::UMotoSynthSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMotoSynthSource::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMotoSynthSourceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (StagedSoundWave.IsValid())
	{
		USoundWave* SoundWave = StagedSoundWave.Get();
		
		// Retrieve the raw uncompressed imported data
		TArray<uint8> ImportedSoundWaveData;
		uint32 ImportedSampleRate;
		uint16 ImportedChannelCount;
		SoundWave->GetImportedSoundWaveData(ImportedSoundWaveData, ImportedSampleRate, ImportedChannelCount);

		if (ImportedSoundWaveData.Num() > 0 && ImportedChannelCount > 0)
		{
			// Warn that we're ignoring non-mono source for motosynth source. Mixing channels to mono would likely destroy the source asset anyway, so we're
			// only going to use the mono channel (left) as the source
			if (ImportedChannelCount > 1)
			{
				UE_LOG(LogSynthesisEditor, Warning, TEXT("Sound source used as moto synth source has more than one channel. Only using the 0th channel index (left) for moto synth source."));
			}

			UMotoSynthSource* NewAsset = NewObject<UMotoSynthSource>(InParent, InName, Flags);

			const int32 NumFrames = (ImportedSoundWaveData.Num() / sizeof(int16)) / ImportedChannelCount;

			NewAsset->SourceSampleRate = ImportedSampleRate;
			NewAsset->SourceData.AddUninitialized(NumFrames);

			int16* ImportedDataPtr = (int16*)ImportedSoundWaveData.GetData();
			float* RawSourceDataPtr = NewAsset->SourceData.GetData();

			// Convert to float and only use the left-channel
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
			{
				const int32 SampleIndex = FrameIndex * ImportedChannelCount;
				float CurrSample = static_cast<float>(ImportedDataPtr[SampleIndex]) / 32768.0f;

				RawSourceDataPtr[FrameIndex] = CurrSample;
			}

			NewAsset->PerformGrainTableAnalysis();

			return NewAsset;
		}

		StagedSoundWave.Reset();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE