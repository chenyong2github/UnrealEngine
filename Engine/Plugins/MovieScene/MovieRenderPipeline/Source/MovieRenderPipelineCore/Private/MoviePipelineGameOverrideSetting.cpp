// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineGameOverrideSetting.h"
#include "Scalability.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineUtils.h"

void UMoviePipelineGameOverrideSetting::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	// Store the cvar values and apply the ones from this setting
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Applying Game Override quality settings and cvars."));
	ApplyCVarSettings(true);
}

void UMoviePipelineGameOverrideSetting::TeardownForPipelineImpl(UMoviePipeline* InPipeline)
{
	// Restore the previous cvar values the user had
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Restoring Game Override quality settings and cvars."));
	ApplyCVarSettings(false);
}

void UMoviePipelineGameOverrideSetting::ApplyCVarSettings(const bool bOverrideValues)
{
	if (bCinematicQualitySettings)
	{
		if (bOverrideValues)
		{
			// Store their previous Scalability settings so we can revert back to them
			PreviousQualityLevels = Scalability::GetQualityLevels();

			// Create a copy and override to the maximum level for each Scalability category
			Scalability::FQualityLevels QualityLevels = PreviousQualityLevels;
			QualityLevels.SetFromSingleQualityLevelRelativeToMax(0);

			// Apply
			Scalability::SetQualityLevels(QualityLevels);
		}
		else
		{
			Scalability::SetQualityLevels(PreviousQualityLevels);
		}
	}

	switch (TextureStreaming)
	{
	case EMoviePipelineTextureStreamingMethod::FullyLoad:
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousFramesForFullUpdate, TEXT("r.Streaming.FramesForFullUpdate"), 0, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousFullyLoadUsedTextures, TEXT("r.Streaming.FullyLoadUsedTextures"), 1, bOverrideValues);
		break;
	case EMoviePipelineTextureStreamingMethod::Disabled:
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousTextureStreaming, TEXT("r.TextureStreaming"), 0, bOverrideValues);
		break;
	default:
		// We don't change their texture streaming settings.
		break;
	}

	if (bUseLODZero)
	{
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousForceLOD, TEXT("r.ForceLOD"), 0, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousSkeletalMeshBias, TEXT("r.SkeletalMeshLODBias"), -10, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousParticleLODBias, TEXT("r.ParticleLODBias"), -10, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousFoliageDitheredLOD, TEXT("foliage.DitheredLOD"), 0, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousFoliageForceLOD, TEXT("foliage.ForceLOD"), 0, bOverrideValues);
	}

	if (bDisableHLODs)
	{
		// It's a command and not an integer cvar (despite taking 1/0), so we can't cache it 
		if(GEngine)
		{
			GEngine->Exec(GetWorld(), TEXT("r.HLOD 0"));
		}
	}

	if (bUseHighQualityShadows)
	{
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousShadowDistanceScale, TEXT("r.Shadow.DistanceScale"), ShadowDistanceScale, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousShadowQuality, TEXT("r.ShadowQuality"), 5, bOverrideValues);
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_FLOAT(PreviousShadowRadiusThreshold, TEXT("r.Shadow.RadiusThreshold"), ShadowRadiusThreshold, bOverrideValues);
	}

	if (bOverrideViewDistanceScale)
	{
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousViewDistanceScale, TEXT("r.ViewDistanceScale"), ViewDistanceScale, bOverrideValues);
	}

	if (bDisableGPUTimeout)
	{
		// This CVAR only exists if the D3D12RHI module is loaded
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT_IF_EXIST(PreviousGPUTimeout, TEXT("r.D3D12.GPUTimeout"), 0, bOverrideValues);
	}
	
	{
		// Disable systems that try to preserve performance in runtime games.
		MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousAnimationUROEnabled, TEXT("a.URO.Enable"), 0, bOverrideValues);
	}

	MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousNeverMuteNonRealtimeAudio, TEXT("au.NeverMuteNonRealtimeAudioDevices"), 1, bOverrideValues);

	// To make sure that the skylight is always valid and consistent accross capture sessions, we enforce a full capture each frame, accepting a small GPU cost.
	MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousSkyLightRealTimeReflectionCaptureTimeSlice, TEXT("r.SkyLight.RealTimeReflectionCapture.TimeSlice"), 0, bOverrideValues);

	// To make sure that the skylight is always valid and consistent accross capture sessions, we enforce a full capture each frame, accepting a small GPU cost.
	MOVIEPIPELINE_STORE_AND_OVERRIDE_CVAR_INT(PreviousVolumetricRenderTarget, TEXT("r.VolumetricRenderTarget"), 0, bOverrideValues);

}

void UMoviePipelineGameOverrideSetting::BuildNewProcessCommandLineImpl(FString& InOutUnrealURLParams, FString& InOutCommandLineArgs) const
{
	if (!IsEnabled())
	{
		return;
	}

	FString CVarCommandLineArgs;
	FString CVarExecArgs;

	// We don't provide the GameMode on the command line argument as we expect NewProcess to boot into an empty map and then it will
	// transition into the correct map which will then use the GameModeOverride setting.
	if (bCinematicQualitySettings)
	{
		CVarCommandLineArgs += TEXT("sg.ViewDistanceQuality=4,sg.AntiAliasingQuality=4,sg.ShadowQuality=4,sg.PostProcessQuality=4,sg.TextureQuality=4,sg.EffectsQuality=4,sg.FoliageQuality=4,sg.ShadingQuality=4,");
	}

	switch (TextureStreaming)
	{
	case EMoviePipelineTextureStreamingMethod::FullyLoad:
		CVarCommandLineArgs += TEXT("r.Streaming.FramesForFullUpdate=0,r.Streaming.FullyLoadUsedTextures=1,");
		break;
	case EMoviePipelineTextureStreamingMethod::Disabled:
		CVarCommandLineArgs += TEXT("r.TextureStreaming=0,");
		break;
	default:
		// We don't change their texture streaming settings.
		break;
	}

	if (bUseLODZero)
	{
		CVarCommandLineArgs += TEXT("r.ForceLOD=0,r.SkeletalMeshLODBias=-10,r.ParticleLODBias=-10,foliage.DitheredLOD=0,foliage.ForceLOD=0");
	}

	if (bDisableHLODs)
	{
		// It's a command and not an integer cvar (despite taking 1/0)
		CVarExecArgs += TEXT("r.HLOD 0,");
	}

	if (bUseHighQualityShadows)
	{
		CVarCommandLineArgs += FString::Printf(TEXT("r.Shadow.DistanceScale=%d,r.ShadowQuality=5,r.Shadow.RadiusThreshold=%f,"), ShadowDistanceScale, ShadowRadiusThreshold);
	}

	if (bOverrideViewDistanceScale)
	{
		CVarCommandLineArgs += FString::Printf(TEXT("r.ViewDistanceScale=%d,"), ViewDistanceScale);
	}

	if (bDisableGPUTimeout)
	{
		CVarCommandLineArgs += TEXT("r.D3D12.GPUTimeout=0,");
	}
	
	{
		CVarCommandLineArgs += FString::Printf(TEXT("a.URO.Enable=%d,"), 0);
	}

	CVarCommandLineArgs += FString::Printf(TEXT("r.SkyLight.RealTimeReflectionCapture.TimeSlice=%d,"), 0);

	// Apply the cvar very early on (device profile) time instead of after the map has loaded if possible
	InOutCommandLineArgs += FString::Printf(TEXT(" -dpcvars=%s -execcmds=%s"), *CVarCommandLineArgs, *CVarExecArgs);
}