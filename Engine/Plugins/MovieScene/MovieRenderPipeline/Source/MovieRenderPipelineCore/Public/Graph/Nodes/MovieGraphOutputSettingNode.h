// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/MovieGraphNode.h"
#include "Misc/FrameRate.h"
#include "Styling/AppStyle.h"
#include "MovieGraphOutputSettingNode.generated.h"


UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphOutputSettingNode : public UMovieGraphSettingNode
{
	GENERATED_BODY()
public:
	UMovieGraphOutputSettingNode();

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive) const override
	{
		static const FText OutputSettingsNodeName = NSLOCTEXT("MoviePipelineGraph", "NodeName_OutputSettings", "Output Settings");
		return OutputSettingsNodeName;
	}

	virtual FText GetMenuCategory() const override
	{
		return NSLOCTEXT("MoviePipelineGraph", "Settings_Category", "Settings");
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		static const FLinearColor OutputSettingsColor = FLinearColor(0.854f, 0.509f, 0.039f);
		return OutputSettingsColor;
	}

	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override
	{
		static const FSlateIcon SettingsIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings");

		OutColor = FLinearColor::White;
		return SettingsIcon;
	}
#endif

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_OutputDirectory : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_FileNameFormat : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_OutputResolution : 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_OutputFrameRate : 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bOverwriteExistingOutput : 1;

	/** What directory should all of our output files be relative to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output", meta=(EditCondition="bOverride_OutputDirectory"))
	FDirectoryPath OutputDirectory;

	/** What format string should the final files use? Can include folder prefixes, and format string ({shot_name}, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output", meta = (EditCondition = "bOverride_FileNameFormat"))
	FString FileNameFormat;

	/** What resolution should our output files be exported at? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output", meta = (EditCondition = "bOverride_OutputResolution"))
	FIntPoint OutputResolution;
	
	/** What frame rate should the output files be exported at? This overrides the Display Rate of the target sequence. If not overwritten, uses the default Sequence Display Rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output", meta=(EditCondition="bOverride_OutputFrameRate"))
	FFrameRate OutputFrameRate;
	
	/** If true, output containers should attempt to override any existing files with the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Output", meta = (EditCondition = "bOverride_bOverwriteExistingOutput"))
	bool bOverwriteExistingOutput;
};
