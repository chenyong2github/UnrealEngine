// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineGameOverrideSetting.h"
#include "Scalability.h"
#include "Engine/World.h"
#include "MovieRenderPipelineCoreModule.h"

#define STORE_AND_OVERRIDE_INT_SETTING(InOutVariable, CVarName, OverrideValue, bApply) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(bApply) \
	{ \
		CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
	} \
	else \
	{ \
		InOutVariable = CVar->GetInt(); \
		CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
	} \
}

#define STORE_AND_OVERRIDE_FLOAT_SETTING(InOutVariable, CVarName, OverrideValue, bApply) \
{ \
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName); \
	if(bApply) \
	{ \
		CVar->Set(InOutVariable, EConsoleVariableFlags::ECVF_SetByConsole); \
	} \
	else \
	{ \
		InOutVariable = CVar->GetFloat(); \
		CVar->Set(OverrideValue, EConsoleVariableFlags::ECVF_SetByConsole); \
	} \
}

void UMoviePipelineGameOverrideSetting::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	// Store the cvar values and apply the ones from this setting
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Applying Game Override quality settings and cvars."));
	ApplyCVarSettings(false);
}

void UMoviePipelineGameOverrideSetting::TeardownForPipelineImpl(UMoviePipeline* InPipeline)
{
	// Restore the previous cvar values the user had
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Restoring Game Override quality settings and cvars."));
	ApplyCVarSettings(true);
}

void UMoviePipelineGameOverrideSetting::ApplyCVarSettings(const bool bRestoreOldValues)
{
	if (!IsEnabled())
	{
		return;
	}

	if (bCinematicQualitySettings)
	{
		if (!bRestoreOldValues)
		{
			// Store their previous Scalability settings so we can revert back to them
			PreviousQualityLevels = Scalability::GetQualityLevels();

			// Create a copy and override to the maximum level for each Scalability category
			Scalability::FQualityLevels QualityLevels = PreviousQualityLevels;
			PreviousQualityLevels.SetFromSingleQualityLevelRelativeToMax(0);

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
		{ 
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.FramesForFullUpdate")); 
			if(bRestoreOldValues) 
			{ 
				CVar->Set(PreviousFramesForFullUpdate, EConsoleVariableFlags::ECVF_SetByConsole); 
			} 
			else 
			{ 
				PreviousFramesForFullUpdate = CVar->GetInt(); 
				CVar->Set(0, EConsoleVariableFlags::ECVF_SetByConsole);
			} 
		}

		STORE_AND_OVERRIDE_INT_SETTING(PreviousFramesForFullUpdate, TEXT("r.Streaming.FramesForFullUpdate"), 0, bRestoreOldValues);
		STORE_AND_OVERRIDE_INT_SETTING(PreviousFullyLoadUsedTextures, TEXT("r.Streaming.FullyLoadUsedTextures"), 1, bRestoreOldValues);
		break;
	case EMoviePipelineTextureStreamingMethod::Disabled:
		STORE_AND_OVERRIDE_INT_SETTING(PreviousTextureStreaming, TEXT("r.TextureStreaming"), 0, bRestoreOldValues);
		break;
	default:
		// We don't change their texture streaming settings.
		break;
	}

	if (bUseLODZero)
	{
		STORE_AND_OVERRIDE_INT_SETTING(PreviousForceLOD, TEXT("r.ForceLOD"), 0, bRestoreOldValues);
		STORE_AND_OVERRIDE_INT_SETTING(PreviousSkeletalMeshBias, TEXT("r.SkeletalMeshLODBias"), -4, bRestoreOldValues);
	}

	if (bDisableHLODs)
	{
		// It's a command and not an integer cvar (despite taking 1/0), so we can't cache it 
		GetWorld()->Exec(GetWorld(), TEXT("r.HLOD 0"));
	}

	if (bUseHighQualityShadows)
	{
		STORE_AND_OVERRIDE_INT_SETTING(PreviousShadowDistanceScale, TEXT("r.Shadow.DistanceScale"), ShadowDistanceScale, bRestoreOldValues);
		STORE_AND_OVERRIDE_INT_SETTING(PreviousShadowQuality, TEXT("r.ShadowQuality"), 5, bRestoreOldValues);
		STORE_AND_OVERRIDE_FLOAT_SETTING(PreviousShadowRadiusThreshold, TEXT("r.Shadow.RadiusThreshold"), ShadowRadiusThreshold, bRestoreOldValues);
	}

	if (bOverrideViewDistanceScale)
	{
		STORE_AND_OVERRIDE_INT_SETTING(PreviousViewDistanceScale, TEXT("r.ViewDistanceScale"), ViewDistanceScale, bRestoreOldValues);
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
		InOutUnrealURLParams += FString::Printf(TEXT("?game=%s"), *GameModeOverride->GetName());
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