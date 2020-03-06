// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImpulseResponse.h"
#include "ConvolutionReverbComponent.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"


UClass* FAssetTypeActions_ImpulseResponse::GetSupportedClass() const
{
	return UImpulseResponse::StaticClass();
}

const TArray<FText>& FAssetTypeActions_ImpulseResponse::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetConvolutionReverbSubmenu", "Convolution Reverb"))
	};

	return SubMenus;
}

UImpulseResponseFactory::UImpulseResponseFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UImpulseResponse::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UImpulseResponseFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UImpulseResponse* NewAsset = NewObject<UImpulseResponse>(InParent, InName, Flags);

	if (StagedSoundWave.IsValid())
	{
		USoundWave* Wave = StagedSoundWave.Get();

		Loader.LoadSoundWave(Wave, [&](const USoundWave* SoundWave, const Audio::FSampleBuffer& LoadedSampleBuffer)
		{
			NewAsset->NumChannels = LoadedSampleBuffer.GetNumChannels();
			NewAsset->NumFrames = LoadedSampleBuffer.GetNumFrames();
			NewAsset->AssetSampleRate = LoadedSampleBuffer.GetSampleRate();

			// Convert to float and de-interleave (TODO: SRC?)
			const int32 NumChannels = NewAsset->NumChannels;
			const int32 NumFrames = NewAsset->NumFrames;
			const int32 NumSamples = NumChannels * NumFrames;

			NewAsset->IRData.Reset();
			NewAsset->IRData.AddUninitialized(NumSamples);

			const int16* RESTRICT InputBuffer = reinterpret_cast<const int16*>(LoadedSampleBuffer.GetData());
			float* RESTRICT OutputBuffer = NewAsset->IRData.GetData();

			for (int32 i = 0; i < NumSamples; ++i)
			{
				float CurrSample = static_cast<float>(InputBuffer[i]) / 32768.0f;
				const int32 CurrFrame = i / NumChannels;
				const int32 CurrChannel = i % NumChannels;
				const int32 CurrOutputIndex = CurrChannel * NumFrames + CurrFrame;
				OutputBuffer[CurrOutputIndex] = CurrSample;
			}
		});

		StagedSoundWave.Reset();
	}

	return NewAsset;
}

#undef LOCTEXT_NAMESPACE