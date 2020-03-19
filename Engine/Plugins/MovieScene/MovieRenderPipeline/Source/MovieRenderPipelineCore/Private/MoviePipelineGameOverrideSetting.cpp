// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineGameOverrideSetting.h"
#include "Scalability.h"
#include "Engine/World.h"
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
	if (!IsEnabled())
	{
		return;
	}

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
	}

	if (bDisableHLODs)
	{
		// It's a command and not an integer cvar (despite taking 1/0), so we can't cache it 
		GetWorld()->Exec(GetWorld(), TEXT("r.HLOD 0"));
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
}

void UMoviePipelineGameOverrideSetting::BuildNewProcessCommandLineImpl(FString& InOutUnrealURLParams, FString& InOutCommandLineArgs) const
{
	if (!IsEnabled())
	{
		return;
	}

	FString CVarCommandLineArgs;
	FString CVarExecArgs;

	// If they've left the setting enabled but set it to None, we don't do anything.
	if (GameModeOverride)
	{
		InOutUnrealURLParams += FString::Printf(TEXT("?game=%s"), *GameModeOverride->GetPathName());
	}

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
		CVarCommandLineArgs += TEXT("r.ForceLOD=0,r.SkeletalMeshLODBias=-4,");
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

	// Apply the cvar very early on (device profile) time instead of after the map has loaded if possible
	InOutCommandLineArgs += FString::Printf(TEXT(" -dpcvars=%s -execcmds=%s"), *CVarCommandLineArgs, *CVarExecArgs);
}