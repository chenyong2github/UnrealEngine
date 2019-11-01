// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MovieRenderPipelineEditorModule.h"
#include "MoviePipelineBackbufferPass.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "GameFramework/GameMode.h"
#include "MoviePipelineAccumulationSetting.h"
#include "MovieRenderPipelineConfig.h"
#include "MoviePipelineImageSequenceContainer.h"
#include "MoviePipelineAudioOutput.h"
#include "Misc/Paths.h"

UMoviePipelineShotConfig* GenerateTestShotConfig(UObject* InOwner, int32 InSampleCount,
	int32 InShutterAngle, EMoviePipelineShutterTiming InFrameTiming, int32 InTileCount,
	int32 InSpatialSampleCount, bool bIsUsingOverlappedTiles, float PadRatioX, float PadRatioY, float AccumulationGamma)
{
	UMoviePipelineShotConfig* OutConfig = NewObject<UMoviePipelineShotConfig>(InOwner);
	UMoviePipelineBackbufferPass* DefaultPass = NewObject<UMoviePipelineBackbufferPass>(InOwner);
	OutConfig->InputBuffers.Add(DefaultPass);

	UMoviePipelineGameOverrideSetting* GamemodeOverride = OutConfig->FindOrAddSetting<UMoviePipelineGameOverrideSetting>();
	GamemodeOverride->GameModeOverride = AGameMode::StaticClass();

	UMoviePipelineAccumulationSetting* Accumulation = OutConfig->FindOrAddSetting< UMoviePipelineAccumulationSetting>();
	Accumulation->TemporalSampleCount = InSampleCount;
	Accumulation->CameraShutterAngle = InShutterAngle;
	Accumulation->TileCount = InTileCount;
	Accumulation->ShutterTiming = InFrameTiming;
	Accumulation->SpatialSampleCount = InSpatialSampleCount;
	Accumulation->bIsUsingOverlappedTiles = bIsUsingOverlappedTiles;
	Accumulation->PadRatioX = PadRatioX;
	Accumulation->PadRatioY = PadRatioY;
	Accumulation->AccumulationGamma = AccumulationGamma;

	return OutConfig;
}

TArray<UMovieRenderPipelineConfig*> FMovieRenderPipelineEditorModule::GenerateTestPipelineConfigs(FSoftObjectPath InSequence)
{
	// int32 ShutterAngles[] = { 180, 360 };
	// EMoviePipelineShutterTiming ShutterTimings[] = { EMoviePipelineShutterTiming::FrameOpen, EMoviePipelineShutterTiming::FrameCenter, EMoviePipelineShutterTiming::FrameClose };
	// FString ShutterTimingNames[] = { TEXT("FOpen"), TEXT("FCenter"), TEXT("FClose") };
	// int32 TemporalSampleCounts[] = { 1, 5 };

	int32 ShutterAngles[] = { 180 };
	EMoviePipelineShutterTiming ShutterTimings[] = { EMoviePipelineShutterTiming::FrameCenter };
	FString ShutterTimingNames[] = { TEXT("FCenter") };
	int32 TemporalSampleCounts[] = { 1 };

	TArray<UMovieRenderPipelineConfig*> Configs;

	for (int32 ShutterAngleIndex = 0; ShutterAngleIndex < UE_ARRAY_COUNT(ShutterAngles); ShutterAngleIndex++)
	{
		for (int32 ShutterTimingIndex = 0; ShutterTimingIndex < UE_ARRAY_COUNT(ShutterTimings); ShutterTimingIndex++)
		{
			for (int32 TemporalSampleCountIndex = 0; TemporalSampleCountIndex < UE_ARRAY_COUNT(TemporalSampleCounts); TemporalSampleCountIndex++)
			{
				// Build a folder string to place this particular test in.
				FString DirectoryName = FString::Printf(TEXT("SA_%d_ST_%s_SC_%d"), ShutterAngles[ShutterAngleIndex], *ShutterTimingNames[ShutterTimingIndex], TemporalSampleCounts[TemporalSampleCountIndex]);

				UMovieRenderPipelineConfig* OutPipeline = NewObject<UMovieRenderPipelineConfig>(GetTransientPackage());
				OutPipeline->Sequence = InSequence;

				int32 SizeX = 1080;
				int32 SizeY = 1920;
				int32 TileX = 2;
				int32 TileY = 2;
				int32 TestNumSamples = 2;
				float PadRatioX = 0.5f;
				float PadRatioY = 0.5f;
				float AccumulationGamma = 1.0f;

				OutPipeline->OutputResolution = FIntPoint(SizeX*TileX, SizeY*TileY);
				OutPipeline->OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("/VideoCaptures/") / *DirectoryName;

				const int32 NumSpatialSamples = TestNumSamples;
				const int32 NumTiles = TileX;
				static bool bIsUsingOverlappedTiles = true;

				UMoviePipelineShotConfig* DefaultConfig = GenerateTestShotConfig(OutPipeline,
					TemporalSampleCounts[TemporalSampleCountIndex],
					ShutterAngles[ShutterAngleIndex],
					ShutterTimings[ShutterTimingIndex],
					NumTiles, NumSpatialSamples, bIsUsingOverlappedTiles, PadRatioX, PadRatioY, AccumulationGamma);

				OutPipeline->DefaultShotConfig = DefaultConfig;

				// Add some Outputs
				UMoviePipelineImageSequenceContainerBase* ImageSequenceOutput = NewObject<UMoviePipelineImageSequenceContainerBase>(OutPipeline);
				OutPipeline->OutputContainers.Add(ImageSequenceOutput);

				UMoviePipelineAudioOutput* AudioOutput = NewObject<UMoviePipelineAudioOutput>(OutPipeline);
				OutPipeline->OutputContainers.Add(AudioOutput);

				// Add it to our list to be processed.
				Configs.Add(OutPipeline);
			}
		}
	}

	return Configs;
}