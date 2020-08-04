// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Containers/ArrayView.h"
#include "Engine/DeveloperSettings.h"
#include "LiveStreamAnimationSettings.generated.h"

/**
 * Common settings for the Live Stream Animation plugin.
 */
UCLASS(Config=Game, DefaultConfig)
class LIVESTREAMANIMATION_API ULiveStreamAnimationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULiveStreamAnimationSettings();

	/**
	 * Get the configured list of Anim Handle Names.
	 */
	static const TArrayView<const FName> GetHandleNames();

	/**
	 * Get the configured list fo Animation Data Handlers.
	 */
	static const TArrayView<const FSoftClassPath> GetConfiguredDataHandlers();

	//~ Begin UDeveloperSettingsObject Interface
	virtual FName GetContainerName() const;
	virtual FName GetCategoryName() const;
	virtual FName GetSectionName() const;

#if WITH_EDITOR
	virtual FText GetSectionText() const;
	virtual FText GetSectionDescription() const;
#endif
	//~ End UDeveloperSettingsObject

private:

	/**
	 * List of names that we know and can use for network handles.
	 *
	 * See ULiveStreamAnimationSubsystem and FLiveStreamAnimationHandle for more information.
	 */
	UPROPERTY(Config, Transient, EditAnywhere, Category = "LiveStreamAnimation")
	TArray<FName> HandleNames;

	UPROPERTY(Config, Transient, EditAnywhere, Category = "LiveStreamAnimation", Meta = (AllowAbstract = "False", AllowedClasses="LiveStreamAnimationDataHandler"))
	TArray<FSoftClassPath> ConfiguredDataHandlers;

	//! Used to track changes to the FrameTranslator so systems running in the Editor / PIE
	//! can update their state.
	FSimpleMulticastDelegate OnFrameTranslatorChanged;
};