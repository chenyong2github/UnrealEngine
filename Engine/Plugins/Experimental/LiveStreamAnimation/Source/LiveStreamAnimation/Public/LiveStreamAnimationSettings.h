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
	 * Get the configured Live Link Frame Translator.
	 *
	 * May return null if one hasn't be set.
	 */
	static class ULiveStreamAnimationLiveLinkFrameTranslator* GetFrameTranslator();

	/**
	 * Register to receive notifications whenever the FrameTranslator is changed.
	 * This should only happen in the Editor when a user changes the settings.
	 */
	static FDelegateHandle AddFrameTranslatorChangedCallback(FSimpleMulticastDelegate::FDelegate&& InDelegate);
	static FDelegateHandle AddFrameTranslatorChangedCallback(const FSimpleMulticastDelegate::FDelegate& InDelegate);

	/**
	 * Unregister from notifications when the FrameTranslator is changed.
	 */
	static void RemoveFrameTranslatorChangedCallback(FDelegateHandle DelegateHandle);

	/**
	 * Get the configured list of Anim Handle Names.
	 */
	static const TArrayView<const FName> GetHandleNames();


#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif

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
	 * The Frame Translator that'll be used to apply networked Live Link packets to usable
	 * animation frames.
	 *
	 * See ULiveStreamAnimationLiveLinkFrameTranslator for more information.
	 */
	UPROPERTY(Config, Transient, EditAnywhere, Category="LiveStreamAnimation")
	TSoftObjectPtr<class ULiveStreamAnimationLiveLinkFrameTranslator> FrameTranslator;

	/**
	 * List of names that we know and can use for network handles.
	 *
	 * See ULiveStreamAnimationSubsystem and FLiveStreamAnimationHandle for more information.
	 */
	UPROPERTY(Config, Transient, EditAnywhere, Category = "LiveStreamAnimation")
	TArray<FName> HandleNames;

	//! Used to track changes to the FrameTranslator so systems running in the Editor / PIE
	//! can update their state.
	FSimpleMulticastDelegate OnFrameTranslatorChanged;
};