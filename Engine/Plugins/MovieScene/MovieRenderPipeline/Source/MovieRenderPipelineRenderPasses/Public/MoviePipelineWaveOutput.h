// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineOutputBase.h"
#include "Sound/SampleBufferIO.h"
#include "MoviePipelineWaveOutput.generated.h"

UCLASS(BlueprintType)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineWaveOutput : public UMoviePipelineOutputBase
{
	GENERATED_BODY()

		UMoviePipelineWaveOutput()
		: FileNameFormat(TEXT("{sequence_name}"))
		, OutstandingWrites(0)
	{
	}

public:
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "AudioSettingDisplayName", ".wav Audio"); }

protected:
	virtual void BeginFinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() override;
	virtual void ValidateStateImpl() override;
	virtual void BuildNewProcessCommandLineImpl(FString& InOutUnrealURLParams, FString& InOutCommandLineArgs) const override;
public:
	/** What format string should the final files use? Can include folder prefixes, and format string ({shot_name}, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output")
	FString FileNameFormat;

private:
	/** Kept alive during finalization because the writer writes async to disk but doesn't expect to fall out of scope */
	TArray<TUniquePtr<Audio::FSoundWavePCMWriter>> ActiveWriters;
	TAtomic<int32> OutstandingWrites;
};