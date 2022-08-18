// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorWaveWriter.h"

#include "AssetToolsModule.h"
#include "DSP/FloatArrayMath.h"
#include "FileHelpers.h"
#include "Sound/SoundWave.h"
#include "WaveformEditorLog.h"

FWaveformEditorWaveWriter::FWaveformEditorWaveWriter(USoundWave* InSoundWave)
	: SourceSoundWave(InSoundWave)
	, WaveWriter(MakeUnique<Audio::FSoundWavePCMWriter>())
{
}

bool FWaveformEditorWaveWriter::CanCreateSoundWaveAsset() const
{
	return SourceSoundWave != nullptr && WaveWriter != nullptr && WaveWriter->IsDone();
}

void FWaveformEditorWaveWriter::ExportTransformedWaveform()
{
	check(SourceSoundWave);
	check(WaveWriter);

	const FString DefaultSuffix = TEXT("_Edited");

	FString AssetName;
	FString PackageName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(SourceSoundWave->GetOutermost()->GetName(), DefaultSuffix, PackageName, AssetName);
	
	FString AssetPath = FPaths::GetPath(PackageName);

	//The wave writer will already be putting 'Game' in front of the provided asset path
	AssetPath = AssetPath.Replace(TEXT("/Game"), TEXT(""), ESearchCase::CaseSensitive); 

	Audio::TSampleBuffer<> BufferToWrite = GenerateSampleBuffer();

	TFunction<void(const USoundWave*)> OnSoundWaveWritten = [AssetName, AssetPath](const USoundWave* ResultingWave) {
		UE_LOG(LogWaveformEditor, Log, TEXT("Finished Exporting edited soundwave %s/%s"), *AssetPath, *AssetName);
		TArray<UPackage*> PackagesToSave;

		if(ResultingWave->GetPackage())
		{
			PackagesToSave.Add(ResultingWave->GetPackage());
			FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false /*bCheckDirty*/, true /*bPromptToSave*/);
		}
	};

	if (!WaveWriter->BeginWriteToSoundWave(AssetName, BufferToWrite, AssetPath, OnSoundWaveWritten))
	{
		UE_LOG(LogWaveformEditor, Log, TEXT("Exporting edited soundwave to %s/%s failed"), *AssetPath, *AssetName);
	}
}


Audio::TSampleBuffer<> FWaveformEditorWaveWriter::GenerateSampleBuffer() const
{
	TArray<uint8> RawPCMData;
	uint16 NumChannels;
	uint32 SampleRate;

	if (!SourceSoundWave->GetImportedSoundWaveData(RawPCMData, SampleRate, NumChannels))
	{
		UE_LOG(LogInit, Warning, TEXT("Failed to get imported soundwave data for file: %s. Edited waveform will not be rendered."), *SourceSoundWave->GetPathName());
		return Audio::TSampleBuffer<>();
	}

	uint32 NumSamples = RawPCMData.Num() * sizeof(uint8) / sizeof(int16);

	Audio::FAlignedFloatBuffer Buffer;
	Buffer.SetNumUninitialized(NumSamples);

	Audio::ArrayPcm16ToFloat(MakeArrayView((int16*)RawPCMData.GetData(), NumSamples), Buffer);

	if (SourceSoundWave->Transformations.Num() > 0)
	{
		Audio::FWaveformTransformationWaveInfo TransformationInfo;

		TransformationInfo.Audio = &Buffer;
		TransformationInfo.NumChannels = NumChannels;
		TransformationInfo.SampleRate = SampleRate;

		TArray<Audio::FTransformationPtr> Transformations = SourceSoundWave->CreateTransformations();

		for (const Audio::FTransformationPtr& Transformation : Transformations)
		{
			Transformation->ProcessAudio(TransformationInfo);
		}

		if (const float MaxValue = Audio::ArrayMaxAbsValue(Buffer) > 1.f)
		{
			Audio::ArrayMultiplyByConstantInPlace(Buffer, 1.f / MaxValue);
		}

		SampleRate = TransformationInfo.SampleRate;
		NumChannels = TransformationInfo.NumChannels;
		NumSamples = Buffer.Num();

		check(NumChannels > 0);
		check(SampleRate > 0);
	}

	Audio::TSampleBuffer<> OutBuffer(Buffer, NumChannels, SampleRate);

	return MoveTemp(OutBuffer);
}