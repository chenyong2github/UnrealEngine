// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineConsoleVariableSetting.h"

#include "MoviePipelineQueue.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/DefaultValueHelper.h"
#include "Sections/MovieSceneConsoleVariableTrackInterface.h"
#include "Sections/MovieSceneCVarSection.h"
#include "Tracks/MovieSceneCVarTrack.h"
#include "Tracks/MovieSceneSubTrack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MoviePipelineConsoleVariableSetting)

#define LOCTEXT_NAMESPACE "MoviePipelineConsoleVariableSetting"

namespace UE
{
	namespace MoviePipeline
	{
		static void SetValue(IConsoleVariable* InCVar, float InValue)
		{
			check(InCVar);

			// When Set is called on a cvar the value is turned into a string. With very large
			// floats this is turned into scientific notation. If the cvar is later retrieved as
			// an integer, the scientific notation doesn't parse into integer correctly. We'll
			// cast to integer first (to avoid scientific notation) if we know the cvar is an integer.
			if (InCVar->IsVariableInt())
			{
				InCVar->SetWithCurrentPriority(static_cast<int32>(InValue));
			}
			else if (InCVar->IsVariableBool())
			{
				InCVar->SetWithCurrentPriority(InValue != 0.f ? true : false);
			}
			else
			{
				InCVar->SetWithCurrentPriority(InValue);
			}
		}

		/**
		 * Determine if the given UMovieScene contains any CVar tracks that are not muted, and also have an
		 * active section with CVars that are set. Sub-sequences will be searched for CVar tracks as well.
		 */
		static bool IsCVarTrackPresent(const UMovieScene* InMovieScene)
		{
			if (!InMovieScene)
			{
				return false;
			}

			for (UMovieSceneTrack* Track : InMovieScene->GetTracks())
			{
				// Process CVar tracks. Return immediately if any of the CVar tracks contain CVars that are set.
				// If this is the case, sub tracks don't need to be searched.
				if (Track->IsA<UMovieSceneCVarTrack>())
				{
					const UMovieSceneCVarTrack* CVarTrack = Cast<UMovieSceneCVarTrack>(Track);
					for (const UMovieSceneSection* Section : CVarTrack->GetAllSections())
					{
						const UMovieSceneCVarSection* CVarSection = Cast<UMovieSceneCVarSection>(Section);
						if (!CVarSection || !MovieSceneHelpers::IsSectionKeyable(CVarSection))
						{
							continue;
						}
						
						// Does this CVar track have anything in it?
						if (!CVarSection->ConsoleVariableCollections.IsEmpty() || !CVarSection->ConsoleVariables.ValuesByCVar.IsEmpty())
						{
							return true;
						}
					}
				}
				
				// Process sub tracks (which could potentially contain other sequences with CVar tracks)
				if (Track->IsA<UMovieSceneSubTrack>())
				{
					const UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track);
					for (const UMovieSceneSection* Section : SubTrack->GetAllSections())
					{
						const UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
						if (!SubSection || !MovieSceneHelpers::IsSectionKeyable(SubSection))
						{
							continue;
						}

						// Recurse into sub-sequences
						if (const UMovieSceneSequence* SubSequence = SubSection->GetSequence())
						{
							if (IsCVarTrackPresent(SubSequence->GetMovieScene()))
							{
								return true;
							}
						}
					}
				}
			}
			
			return false;
		}
	}
}

#if WITH_EDITOR

FText UMoviePipelineConsoleVariableSetting::GetFooterText(UMoviePipelineExecutorJob* InJob) const
{
	if (!InJob)
	{
		return FText();
	}
	
	const ULevelSequence* LoadedSequence = Cast<ULevelSequence>(InJob->Sequence.TryLoad());
	if (!LoadedSequence)
	{
		return FText();
	}
	
	if (!UE::MoviePipeline::IsCVarTrackPresent(LoadedSequence->MovieScene))
	{
		return FText();
	}
	
	return FText(LOCTEXT(
		"SequencerCvarWarning",
		"The current job contains a Level Sequence with a Console Variables Track, additional settings are configured in Sequencer."));
}

#endif // WITH_EDITOR

void UMoviePipelineConsoleVariableSetting::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	ApplyCVarSettings(true);
}

void UMoviePipelineConsoleVariableSetting::TeardownForPipelineImpl(UMoviePipeline* InPipeline)
{
	ApplyCVarSettings(false);
}
	
void UMoviePipelineConsoleVariableSetting::ApplyCVarSettings(const bool bOverrideValues)
{
	if (bOverrideValues)
	{
		MergeConsoleVariables();
		PreviousConsoleVariableValues.Reset();
		PreviousConsoleVariableValues.SetNumZeroed(MergedConsoleVariables.Num());
	}

	int32 Index = 0;
	for(const TPair<FString, float>& KVP : MergedConsoleVariables)
	{
		// We don't use the shared macro here because we want to soft-warn the user instead of tripping an ensure over missing cvar values.
		const FString TrimmedCvar = KVP.Key.TrimStartAndEnd();
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*TrimmedCvar); 
		if (CVar)
		{
			if (bOverrideValues)
			{
				PreviousConsoleVariableValues[Index] = CVar->GetFloat();
				UE::MoviePipeline::SetValue(CVar, KVP.Value);
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Applying CVar \"%s\" PreviousValue: %f NewValue: %f"), *KVP.Key, PreviousConsoleVariableValues[Index], KVP.Value);
			}
			else
			{
				UE::MoviePipeline::SetValue(CVar, PreviousConsoleVariableValues[Index]);
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Restoring CVar \"%s\" PreviousValue: %f NewValue: %f"), *KVP.Key, KVP.Value, PreviousConsoleVariableValues[Index]);
			}
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to apply CVar \"%s\" due to no cvar by that name. Ignoring."), *KVP.Key);
		}

		Index++;
	}

	if (bOverrideValues)
	{
		for (const FString& Command : StartConsoleCommands)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Executing Console Command \"%s\" before shot starts."), *Command);
			UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), Command, nullptr);
		}
	}
	else
	{
		for (const FString& Command : EndConsoleCommands)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Executing Console Command \"%s\" after shot ends."), *Command);
			UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), Command, nullptr);
		}
	}
}

void UMoviePipelineConsoleVariableSetting::MergeConsoleVariables()
{
	MergedConsoleVariables.Reset();
	
	// Merge in the presets
	for (const TScriptInterface<IMovieSceneConsoleVariableTrackInterface>& Preset : ConsoleVariablePresets)
	{
		if (!Preset)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Invalid CVar preset specified. Ignoring."));
			continue;
		}
		
		const bool bOnlyIncludeChecked = true;
		TArray<TTuple<FString, FString>> PresetCVars;
		Preset->GetConsoleVariablesForTrack(bOnlyIncludeChecked, PresetCVars);
		
		for (const TTuple<FString, FString>& CVarPair : PresetCVars)
		{
			float CVarFloatValue = 0.0f;
			if (FDefaultValueHelper::ParseFloat(CVarPair.Value, CVarFloatValue))
			{
				MergedConsoleVariables.Add(CVarPair.Key, CVarFloatValue);
			}
			else
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to apply CVar \"%s\" (from preset \"%s\") because value could not be parsed into a float. Ignoring."),
					*CVarPair.Key, *Preset.GetObject()->GetName());
			}
		}
	}
	
	// Merge in the overrides
	for (const TPair<FString, float>& KVP : ConsoleVariables)
	{
		MergedConsoleVariables.Add(KVP);
	}
}

#undef LOCTEXT_NAMESPACE