// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Containers/ArrayView.h"
#include "Engine/DeveloperSettings.h"
#include "LSALiveLinkSettings.generated.h"

UCLASS(Config=Game, DefaultConfig)
class LSALIVELINK_API ULSALiveLinkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	/**
	 * Get the configured Live Link Frame Translator.
	 *
	 * May return null if one hasn't be set.
	 */
	static class ULSALiveLinkFrameTranslator* GetFrameTranslator();

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

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif

	//~ Begin UDeveloperSettingsObject Interface
	virtual FName GetCategoryName() const;
	//~ End UDeveloperSettingsObject

private:

	/**
	 * The Frame Translator that'll be used to apply networked Live Link packets to usable
	 * animation frames.
	 *
	 * See ULSALiveLinkFrameTranslator for more information.
	 */
	UPROPERTY(Config, Transient, EditAnywhere, Category="LSALiveLink")
	TSoftObjectPtr<class ULSALiveLinkFrameTranslator> FrameTranslator;

	//! Used to track changes to the FrameTranslator so systems running in the Editor / PIE
	//! can update their state.
	FSimpleMulticastDelegate OnFrameTranslatorChanged;
};