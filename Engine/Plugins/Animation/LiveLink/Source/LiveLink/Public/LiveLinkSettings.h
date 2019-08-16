// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "Engine/EngineTypes.h"
#include "LiveLinkRole.h"
#include "LiveLinkFrameInterpolationProcessor.h"
#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkSubjectSettings.h"
#include "Templates/SubclassOf.h"

#include "LiveLinkSettings.generated.h"


/**
 * Settings for LiveLinkRole.
 */
USTRUCT()
struct LIVELINK_API FLiveLinkRoleProjectSetting
{
	GENERATED_BODY()

public:
	FLiveLinkRoleProjectSetting();

public:
	/** The role of the current setting. */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkRole> Role;

	/** The settings class to use for the subject. If null, LiveLinkSubjectSettings will be used by default. */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkSubjectSettings> SettingClass;

	/** The interpolation to use for the subject. If null, no interpolation will be performed. */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkFrameInterpolationProcessor> FrameInterpolationProcessor;

	/** The pre processors to use for the subject. */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TArray<TSubclassOf<ULiveLinkFramePreProcessor>> FramePreProcessors;
};


/**
 * Settings for LiveLink.
 */
UCLASS(config=Game, defaultconfig)
class LIVELINK_API ULiveLinkSettings : public UObject
{
	GENERATED_BODY()

public:
	ULiveLinkSettings();

protected:
	UPROPERTY(config, EditAnywhere, Category="LiveLink")
	TArray<FLiveLinkRoleProjectSetting> DefaultRoleSettings;

public:
	/** The default location in which to save take presets */
	UPROPERTY(config, EditAnywhere, Category="LiveLink", meta=(DisplayName="Preset Save Location"))
	FDirectoryPath PresetSaveDir;

	/** The refresh frequency of the list of message bus provider (when discovery is requested). */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired=true, ForceUnits=s))
	double MessageBusPingRequestFrequency;

	/** The refresh frequency of the heartbeat when a provider didn't send us an updated. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired=true, ForceUnits=s))
	double MessageBusHeartbeatFrequency;

	/** How long we should wait before a provider become unresponsive. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired=true, ForceUnits=s))
	double MessageBusHeartbeatTimeout;

	/**
	 * A source may still exist but do not send frames for a subject.
	 * Time before considering the subject as "invalid".
	 * The subject still exist and can still be evaluated.
	 * An invalid subject is shown as yellow in the LiveLink UI.
	 */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI", meta=(ForceUnits=s))
	double TimeWithoutFrameToBeConsiderAsInvalid;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI")
	FLinearColor ValidColor;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI")
	FLinearColor InvalidColor;

public:
	FLiveLinkRoleProjectSetting GetDefaultSettingForRole(TSubclassOf<ULiveLinkRole> Role) const;
	const FDirectoryPath& GetPresetSaveDir() const { return PresetSaveDir; }
	double GetTimeWithoutFrameToBeConsiderAsInvalid() const { return TimeWithoutFrameToBeConsiderAsInvalid; }
	FLinearColor GetValidColor() const { return ValidColor; }
	FLinearColor GetInvalidColor() const { return InvalidColor; }
	float GetMessageBusPingRequestFrequency() const { return MessageBusPingRequestFrequency; }
	float GetMessageBusHeartbeatFrequency() const { return MessageBusHeartbeatFrequency; }
	double GetMessageBusHeartbeatTimeout() const { return MessageBusHeartbeatTimeout; }
};
