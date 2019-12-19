// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "Engine/EngineTypes.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineOutputSetting.generated.h"

// Forward Declares
class AGameModeBase;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineOutputSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	UMoviePipelineOutputSetting();
public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "OutputSettingDisplayName", "Output"); }
	virtual FText GetFooterText(UMoviePipelineExecutorJob* InJob) const override;
#endif
	virtual bool IsValidOnShots() const override { return false; }
	virtual bool IsValidOnMaster() const override { return true; }
	virtual void GetFilenameFormatArguments(FFormatNamedArguments& OutArguments, const UMoviePipelineExecutorJob* InJob) const;
public:
	/** What directory should all of our output files be relative to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	FDirectoryPath OutputDirectory;
	
	/** What format string should the final files use? Can include folder prefixes, and format string ({shot_name}, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	FString FileNameFormat;

	/** What resolution should our output files be exported at? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	FIntPoint OutputResolution;
	
	/** Should we use the custom frame rate specified by OutputFrameRate? Otherwise defaults to Sequence frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	bool bUseCustomFrameRate;
	
	/** What frame rate should the output files be exported at? This overrides the Display Rate of the target sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomFrameRate), Category = "File Output")
	FFrameRate OutputFrameRate;
	
	/** How many frames (in Display Rate) should we step at a time. Can be used to render every other frame for faster drafts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	FFrameNumber OutputFrameStep;
	
	/** If true, output containers should attempt to override any existing files with the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	bool bOverrideExistingOutput;
	
public:
	/** If true, override the Playback Range start/end bounds with the bounds specified below.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	bool bUseCustomPlaybackRange;
	
	/** Used when overriding the playback range. In Display Rate frames. If bUseCustomPlaybackRange is false range will come from Sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomPlaybackRange), Category = "File Output")
	FFrameNumber CustomStartFrame;
	
	/** Used when overriding the playback range. In Display Rate frames. If bUseCustomPlaybackRange is false range will come from Sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition=bUseCustomPlaybackRange), Category = "File Output")
	FFrameNumber CustomEndFrame;
};