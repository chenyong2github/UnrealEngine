// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "MoviePipelineConsoleVariableSetting.generated.h"

class IMovieSceneConsoleVariableTrackInterface;

UCLASS(BlueprintType)
class MOVIERENDERPIPELINESETTINGS_API UMoviePipelineConsoleVariableSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "ConsoleVariableSettingDisplayName", "Console Variables"); }
	virtual FText GetFooterText(UMoviePipelineExecutorJob* InJob) const override;
#endif
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnPrimary() const override { return true; }
	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) override;
	virtual void TeardownForPipelineImpl(UMoviePipeline* InPipeline) override;

	// This needs to be higher priority than the Game Override setting so that the values the user specifies for cvars here are the ones actually applied during renders
	// otherwise the Scalability Settings of the Game Override setting can change values away from what the user expects.
	virtual int32 GetPriority() const override { return 1; }
protected:
	void ApplyCVarSettings(const bool bOverrideValues);

public:
	// Note that the interface is used here instead of directly using UConsoleVariablesAsset in order to not
	// depend on the Console Variables Editor.
	/**
	 * An array of presets from the Console Variables Editor. The preset cvars will be applied (in the order they are
	 * specified) before any of the cvars in "Console Variables". In other words, cvars in "Console Variables" will
	 * take precedence over the cvars coming from these presets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<TScriptInterface<IMovieSceneConsoleVariableTrackInterface>> ConsoleVariablePresets;
	
	/** 
	* An array of key/value pairs for console variable name and the value you wish to set for that cvar.
	* The existing value will automatically be cached and restored afterwards.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TMap<FString, float> ConsoleVariables;

	/**
	* An array of console commands to execute when this shot is started. If you need to restore the value 
	* after the shot, add a matching entry in the EndConsoleCommands array. Because they are commands
	* and not values we cannot save the preivous value automatically.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FString> StartConsoleCommands;

	/**
	* An array of console commands to execute when this shot is finished. Used to restore changes made by
	* StartConsoleCommands.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FString> EndConsoleCommands;

private:
	/** Merge together preset and override cvars into MergedConsoleVariables. Discards result of a prior merge (if any). */
	void MergeConsoleVariables();

private:
	TArray<float> PreviousConsoleVariableValues;

	/** Merged result of preset cvars and override cvars. */
	TMap<FString, float> MergedConsoleVariables;
};
