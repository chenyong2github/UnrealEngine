// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MoviePipelineSetting.generated.h"

class UMoviePipeline;
struct FSlateBrush;

/**
* A base class for all Movie Render Pipeline settings.
*/
UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineSetting : public UObject
{
	GENERATED_BODY()
		
public:
	/**
	* Called during setup in the relevant portion of the Movie Pipeline lifecycle.
	* Can get called multiple times if a setting is shared between shots.
	*/
	void SetupForPipeline(UMoviePipeline* InPipeline);

	/**
	* Called during shutdown in the relevant portion of the Movie Pipeline lifecycle.
	* Can get called multiple times if a setting is shared between shots.
	*/
	void TeardownForPipeline(UMoviePipeline* InPipeline) { TeardownForPipelineImpl(InPipeline); }

	// UObject Interface
	virtual UWorld* GetWorld() const override;
	// ~UObject Interface

protected:
	UMoviePipeline* GetPipeline() const;

	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) {}
	virtual void TeardownForPipelineImpl(UMoviePipeline* InPipeline) {}
	
public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const { return this->GetClass()->GetDisplayNameText(); }
#endif
	/** Can this configuration setting be added to shots? If not, it will throw an error when trying to add it to a shot config. */
	virtual bool IsValidOnShots() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnShots, return false; );
	/** Can this configuration setting be added to the master configuration? If not, it will throw an error when trying to add it to the master configuration. */
	virtual bool IsValidOnMaster() const PURE_VIRTUAL(UMoviePipelineSetting::IsValidOnMaster, return false; );

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