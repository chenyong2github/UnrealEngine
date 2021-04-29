// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Internationalization/Text.h"
#include "MoviePipelineSetting.generated.h"

class UMoviePipeline;
struct FSlateBrush;
struct FMoviePipelineFormatArgs;
class UMoviePipelineExecutorJob;

enum class EMoviePipelineValidationState : uint8
{
	Valid = 0,
	Warnings = 1,
	Errors = 2
};

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
	*/
	void OnMoviePipelineInitialized(UMoviePipeline* InPipeline);

	/**
	* This is called once on a setting when the movie pipeline is shut down. If the setting
	* only exists as part of a shot override, it will be called once the shot is finished.
	* see shot-related callbacks so that they work properly with shot-overrides.
	*/
	void OnMoviePipelineShutdown(UMoviePipeline* InPipeline) { TeardownForPipelineImpl(InPipeline); }

	/**
	* When rendering in a new process some settings may need to provide command line arguments
	* to affect engine settings that need to be set before most of the engine boots up. This function
	* allows a setting to provide these when the user wants to run in a separate process. This won't
	* be used when running in the current process because it is too late to modify the command line.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void BuildNewProcessCommandLine(UPARAM(ref) FString& InOutUnrealURLParams, UPARAM(ref) FString& InOutCommandLineArgs) const { BuildNewProcessCommandLineImpl(InOutUnrealURLParams, InOutCommandLineArgs); }

	/**
	* Attempt to validate the configuration the user has chosen for this setting. Caches results for fast lookup in UI later.
	*/
	void ValidateState() { ValidateStateImpl(); }

	// UObject Interface
	virtual UWorld* GetWorld() const override;
	// ~UObject Interface

	// Post Finalize Export
	bool HasFinishedExporting() { return HasFinishedExportingImpl(); }
	void BeginExport() { BeginExportImpl(); }
	// ~Post Finalize Export

protected:
	UMoviePipeline* GetPipeline() const;

	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) {}
	virtual void TeardownForPipelineImpl(UMoviePipeline* InPipeline) {}
	
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Warning: This gets called on the CDO of the object */
	virtual FText GetDisplayText() const { return this->GetClass()->GetDisplayNameText(); }
	/** Warning: This gets called on the CDO of the object */
	virtual FText GetCategoryText() const { return NSLOCTEXT("MovieRenderPipeline", "DefaultCategoryName_Text", "Settings"); }

	/** Return a string to show in the footer of the details panel. Will be combined with other selected settings. */
	virtual FText GetFooterText(UMoviePipelineExecutorJob* InJob) const { return FText(); }

	/** Can this setting be disabled? UI only. */
	virtual bool CanBeDisabled() const { return true; }

	/** What icon should this setting use when displayed in the tree list. */
	const FSlateBrush* GetDisplayIcon() { return nullptr; }

	/** What tooltip should be displayed for this setting when hovered in the tree list? */
	FText GetDescriptionText() { return FText(); }
#endif
	/** Can this configuration setting be added to shots? If not, it will throw an error when trying to add it to a shot config. */
	virtual bool IsValidOnShots() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnShots, return false; );
	/** Can this configuration setting be added to the master configuration? If not, it will throw an error when trying to add it to the master configuration. */
	virtual bool IsValidOnMaster() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnMaster, return false; );
	/**
	* If true, then this setting will be included when searching for settings even if it was added transiently. This is used for the rare case where a setting
	* needs to be run (to set reasonable default values) even if the user hasn't added it.
	*/
	virtual bool IgnoreTransientFilters() const { return false; }

	// Validation
	/** What is the result of the last validation? Only valid if the setting has had ValidateState() called on it. */
	virtual EMoviePipelineValidationState GetValidationState() const { return ValidationState; }

	/** Attempt to validate the configuration the user has chosen for this setting. Caches results for fast lookup in UI later. */
	virtual void ValidateStateImpl() { ValidationResults.Reset(); ValidationState = EMoviePipelineValidationState::Valid; }

	/** Get a human-readable text describing what validation errors (if any) the call to ValidateState() produced. */
	virtual TArray<FText> GetValidationResults() const;

	/** Return Key/Value pairs that you wish to be usable in the Output File Name format string or file metadata. This allows settings to add format strings based on their values. */
	virtual void GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const {}
	
	/** Modify the Unreal URL and Command Line Arguments when preparing the setting to be run in a new process. */
	virtual void BuildNewProcessCommandLineImpl(FString& InOutUnrealURLParams, FString& InOutCommandLineArgs) const { }

	/** Can only one of these settings objects be active in a valid pipeline? */
	virtual bool IsSolo() const { return true; }
	
	/** Is this setting enabled by the user in the UI? */
	virtual bool IsEnabled() const { return bEnabled; }
	virtual void SetIsEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	/** Has this setting finished any export-related things it needs to do post-finalize? */
	virtual bool HasFinishedExportingImpl() { return true; }
	/** Called once when all files have been finalized. */
	virtual void BeginExportImpl() { }
	
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UMoviePipeline> CachedPipeline;

	/** Is this setting currently enabled? Disabled settings are like they never existed. */
	UPROPERTY()
	bool bEnabled;
protected:
	/** What was the result of the last call to ValidateState() */
	EMoviePipelineValidationState ValidationState;

	/** If ValidationState isn't valid, what text do we want to show the user to explain to them why? */
	TArray<FText> ValidationResults;
};