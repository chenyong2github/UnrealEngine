// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Internationalization/Text.h"
#include "MoviePipelineSetting.generated.h"

class UMoviePipeline;
struct FSlateBrush;
class UMoviePipelineExecutorJob;

/**
* A base class for all Movie Render Pipeline settings.
*/
UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineSetting : public UObject
{
	GENERATED_BODY()
		
public:
	UMoviePipelineSetting();

	/**
	* This is called once on a setting when the movie pipeline is first set up. If the setting
	* only exists as part of a shot override, it will be called once when the shot is initialized.
	* 
	* This should be used for resource initialization if needed, if you need to change settings
	* see shot-related callbacks so that they work properly with shot-overrides.
	*/
	void OnMoviePipelineInitialized(UMoviePipeline* InPipeline);

	/**
	* This is called once on a setting when the movie pipeline is shut down. If the setting
	* only exists as part of a shot override, it will be called once the shot is finished.
	*
	* This should be used for releasing resources if needed, if you need to change settings
	* see shot-related callbacks so that they work properly with shot-overrides.
	*/
	void OnMoviePipelineShutdown(UMoviePipeline* InPipeline) { TeardownForPipelineImpl(InPipeline); }

	// UObject Interface
	virtual UWorld* GetWorld() const override;
	// ~UObject Interface

protected:
	UMoviePipeline* GetPipeline() const;

	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) {}
	virtual void TeardownForPipelineImpl(UMoviePipeline* InPipeline) {}
	
public:
#if WITH_EDITOR
	/** Warning: This gets called on the CDO of the object */
	virtual FText GetDisplayText() const { return this->GetClass()->GetDisplayNameText(); }
	/** Warning: This gets called on the CDO of the object */
	virtual FText GetCategoryText() const { return NSLOCTEXT("MovieRenderPipeline", "DefaultCategoryName_Text", "Default"); }

	/** Return a string to show in the footer of the details panel. Will be combined with other selected settings. */
	virtual FText GetFooterText(UMoviePipelineExecutorJob* InJob) const { return FText(); }
#endif
	/** Can this configuration setting be added to shots? If not, it will throw an error when trying to add it to a shot config. */
	virtual bool IsValidOnShots() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnShots, return false; );
	/** Can this configuration setting be added to the master configuration? If not, it will throw an error when trying to add it to the master configuration. */
	virtual bool IsValidOnMaster() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnMaster, return false; );

	virtual void GetFilenameFormatArguments(FFormatNamedArguments& OutArguments, const UMoviePipelineExecutorJob* InJob) const {}

	/** Should the Pipeline automatically create an instance of this under the hood so calling code can rely on it existing? */
	virtual bool IsRequired() const { return false; }
	
	/** Can only one of these settings objects be active in a valid pipeline? */
	virtual bool IsSolo() const { return true; }
	
	/** Is this setting valid? Return false and add a reason it's not valid to the array if not. */
	virtual bool ValidatePipeline(TArray<FText>& OutValidationErrors) const { return true; }
	
	/** What icon should this setting use when displayed in the tree list. */
	const FSlateBrush* GetDisplayIcon() { return nullptr; }
	
	/** What tooltip should be displayed for this setting when hovered in the tree list? */
	FText GetDescriptionText() { return FText(); }
	
	/** Is this setting currently enabled? Disabled settings are like they never existed. */
	bool bEnabled;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UMoviePipeline> CachedPipeline;
};