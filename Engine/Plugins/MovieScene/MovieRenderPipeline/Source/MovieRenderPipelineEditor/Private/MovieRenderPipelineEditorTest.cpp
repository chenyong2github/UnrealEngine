// Copyright Epic Games, Inc. All Rights Reserved.
#include "MovieRenderPipelineEditorModule.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MoviePipelineAccumulationSetting.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineAudioOutput.h"
#include "Misc/Paths.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineCameraSetting.h"
#include "GameFramework/GameModeBase.h"

UMoviePipelineShotConfig* GenerateTestShotConfig(UObject* InOwner, int32 InSampleCount,
	int32 InShutterAngle, EMoviePipelineShutterTiming InFrameTiming, int32 InTileCount,
	int32 InSpatialSampleCount, float PadRatioX, float PadRatioY, float AccumulationGamma)
{
	UMoviePipelineShotConfig* OutConfig = NewObject<UMoviePipelineShotConfig>(InOwner);
	OutConfig->FindOrAddSetting<UMoviePipelineDeferredPassBase>();

	UMoviePipelineAccumulationSetting* Accumulation = OutConfig->FindOrAddSetting< UMoviePipelineAccumulationSetting>();
	Accumulation->SpatialSampleCount = InSpatialSampleCount;
	Accumulation->AccumulationGamma = AccumulationGamma;

	{
		UMoviePipelineCameraSetting* CameraSetting = OutConfig->FindOrAddSetting<UMoviePipelineCameraSetting>();
		CameraSetting->TemporalSampleCount = InSampleCount;
		CameraSetting->CameraShutterAngle = InShutterAngle;
		CameraSetting->ShutterTiming = InFrameTiming;
		CameraSetting->bManualExposure = false;
		CameraSetting->ExposureCompensation = 10;
	}

	{
		UMoviePipelineHighResSetting* HighRes = OutConfig->FindOrAddSetting<UMoviePipelineHighResSetting>();
		HighRes->TileCount = InTileCount;
		HighRes->OverlapPercentage = PadRatioX;
		// HighRes->bWriteAllSamples = true;
		// HighRes->OverlapPercentage = 0.f;
	}

	return OutConfig;
}

TArray<UMoviePipelineMasterConfig*> FMovieRenderPipelineEditorModule::GenerateTestPipelineConfigs()
{
	// int32 ShutterAngles[] = { 180, 360 };
	// EMoviePipelineShutterTiming ShutterTimings[] = { EMoviePipelineShutterTiming::FrameOpen, EMoviePipelineShutterTiming::FrameCenter, EMoviePipelineShutterTiming::FrameClose };
	// FString ShutterTimingNames[] = { TEXT("FOpen"), TEXT("FCenter"), TEXT("FClose") };
	// int32 TemporalSampleCounts[] = { 1, 5 };

	int32 ShutterAngles[] = { 180 };
	EMoviePipelineShutterTiming ShutterTimings[] = { EMoviePipelineShutterTiming::FrameCenter };
	FString ShutterTimingNames[] = { TEXT("FCenter") };
	int32 TemporalSampleCounts[] = { 1 };

	TArray<UMoviePipelineMasterConfig*> Configs;

	for (int32 ShutterAngleIndex = 0; ShutterAngleIndex < UE_ARRAY_COUNT(ShutterAngles); ShutterAngleIndex++)
	{
		for (int32 ShutterTimingIndex = 0; ShutterTimingIndex < UE_ARRAY_COUNT(ShutterTimings); ShutterTimingIndex++)
		{
			for (int32 TemporalSampleCountIndex = 0; TemporalSampleCountIndex < UE_ARRAY_COUNT(TemporalSampleCounts); TemporalSampleCountIndex++)
			{
				// Build a folder string to place this particular test in.
				FString DirectoryName = FString::Printf(TEXT("SA_%d_ST_%s_SC_%d"), ShutterAngles[ShutterAngleIndex], *ShutterTimingNames[ShutterTimingIndex], TemporalSampleCounts[TemporalSampleCountIndex]);

				UMoviePipelineMasterConfig* OutPipeline = NewObject<UMoviePipelineMasterConfig>(GetTransientPackage());
				UMoviePipelineOutputSetting* OutputSetting = OutPipeline->FindOrAddSetting<UMoviePipelineOutputSetting>();

				int32 SizeX = 1920;
				int32 SizeY = 1080;
				int32 TileX = 4;
				int32 TileY = 4;
				int32 TestNumSamples = 8;
				float PadRatioX = 0.5f;
				float PadRatioY = 0.5f;
				float AccumulationGamma = 1.0f;

				OutputSetting->OutputResolution = FIntPoint(SizeX*TileX, SizeY*TileY);
				OutputSetting->OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("/VideoCaptures/") / *DirectoryName;

				const int32 NumSpatialSamples = TestNumSamples;
				const int32 NumTiles = TileX;

				UMoviePipelineShotConfig* DefaultConfig = GenerateTestShotConfig(OutPipeline,
					TemporalSampleCounts[TemporalSampleCountIndex],
					ShutterAngles[ShutterAngleIndex],
					ShutterTimings[ShutterTimingIndex],
					NumTiles, NumSpatialSamples, PadRatioX, PadRatioY, AccumulationGamma);

				OutPipeline->DefaultShotConfig = DefaultConfig;

				// Add some Outputs
				OutPipeline->FindOrAddSetting<UMoviePipelineImageSequenceOutput_EXR>();
				OutPipeline->FindOrAddSetting<UMoviePipelineImageSequenceOutput_PNG>();

				UMoviePipelineGameOverrideSetting* GamemodeOverride = OutPipeline->FindOrAddSetting<UMoviePipelineGameOverrideSetting>();
				GamemodeOverride->GameModeOverride = AGameModeBase::StaticClass();

				// Add it to our list to be processed.
				Configs.Add(OutPipeline);
			}
		}
	}

	return Configs;
}