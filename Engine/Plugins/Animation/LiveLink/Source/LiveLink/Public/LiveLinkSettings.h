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
	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkRole> Role;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkSubjectSettings> SettingClass;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink")
	TSubclassOf<ULiveLinkFrameInterpolationProcessor> FrameInterpolationProcessor;

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

protected:
	UPROPERTY(config, EditAnywhere, Category="LiveLink")
	TArray<FLiveLinkRoleProjectSetting> DefaultRoleSettings;

public:
	/** The default location in which to save take presets */
	UPROPERTY(config, EditAnywhere, Category="LiveLink", meta=(DisplayName="Preset Save Location"))
	FDirectoryPath PresetSaveDir;

	/** The refresh frequency of the list of message bus provider (when discovery is requested). */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired=true))
	double MessageBusPingRequestFrequency = 1.0;

	/** The refresh frequency of the heartbeat when a provider didn't send us an updated. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired = true))
	double MessageBusHeartbeatFrequency = 1.0;

	/** How long we should wait before a provider become unresponsive. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category="LiveLink", meta=(ConfigRestartRequired = true))
	double MessageBusHeartbeatTimeout = 2.0;

	/**
	 * A source may still exist but do not send frames for a subject.
	 * Time before considering the subject as "invalid".
	 * The subject still exist and can still be evaluated.
	 * An invalid subject is shown as yellow in the LiveLink UI.
	 */
	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI")
	double TimeWithoutFrameToBeConsiderAsInvalid = 0.5;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI")
	FLinearColor ValidColor = FLinearColor::Green;

	UPROPERTY(config, EditAnywhere, Category = "LiveLink|UI")
	FLinearColor InvalidColor = FLinearColor::Yellow;

public:
	FLiveLinkRoleProjectSetting GetDefaultSettingForRole(TSubclassOf<ULiveLinkRole> Role) const
	{
		int32 IndexOf = DefaultRoleSettings.IndexOfByPredicate([Role](const FLiveLinkRoleProjectSetting& Other){return Other.Role == Role;});
		if (IndexOf != INDEX_NONE)
		{
			return DefaultRoleSettings[IndexOf];
		}
		return FLiveLinkRoleProjectSetting();
	}

	const FDirectoryPath& GetPresetSaveDir() const { return PresetSaveDir; }
	double GetTimeWithoutFrameToBeConsiderAsInvalid() const { return TimeWithoutFrameToBeConsiderAsInvalid; }
	FLinearColor GetValidColor() const { return ValidColor; }
	FLinearColor GetInvalidColor() const { return InvalidColor; }
	float GetMessageBusPingRequestFrequency() const { return MessageBusPingRequestFrequency; }
	float GetMessageBusHeartbeatFrequency() const { return MessageBusHeartbeatFrequency; }
	double GetMessageBusHeartbeatTimeout() const { return MessageBusHeartbeatTimeout; }
};
