// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "WindowsMixedRealityRuntimeSettings.generated.h"

/**
* Add a default value for every new UPROPERTY in this class in <UnrealEngine>/Engine/Config/BaseEngine.ini
*/

DECLARE_DELEGATE_TwoParams(FWindowsMixedRealityRemotingStatusChanged, FString /*RemotingMessage*/, FLinearColor /*StatusColor*/);

/**
 * Implements the settings for the WindowsMixedReality runtime platform.
 */
UCLASS(config=EditorPerProjectUserSettings)
class WINDOWSMIXEDREALITYRUNTIMESETTINGS_API UWindowsMixedRealityRuntimeSettings : public UObject
{
public:
	GENERATED_BODY()

	FWindowsMixedRealityRemotingStatusChanged OnRemotingStatusChanged;

private:
	static class UWindowsMixedRealityRuntimeSettings* WMRSettingsSingleton;

public:
	static UWindowsMixedRealityRuntimeSettings* Get();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Holographic Remoting", Meta = (ConfigRestartRequired = true, DisplayName = "Enable Remoting For Editor (Requires Restart)", Tooltip = "If true WMR is a valid HMD even if none is connected so that one could connect via remoting.  Editor restart required."))
	bool bEnableRemotingForEditor = false;

	/** The IP of the HoloLens to remote to. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Holographic Remoting", Meta = (EditCondition = "bEnableRemotingForEditor", DisplayName = "IP of HoloLens to remote to."))
	FString RemoteHoloLensIP;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Holographic Remoting", Meta = (EditCondition = "bEnableRemotingForEditor", DisplayName = "Max network transfer rate (kb/s)"))
	unsigned int MaxBitrate = 4000;
	
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Holographic Remoting", Meta = (DeprecatedProperty, DisplayName = "HoloLens 1 Remoting (Deprecated, removing for UE5)", Tooltip = "If True remoting connect will assume the device being connected is a HL1, if False HL2 is assumed.  If you chose wrong remoting will fail to connect.  Hololens1 remoting is deprecated and will be removed for UE5.", DeprecationMessage = "Hololens1 remoting is deprecated and will be removed for UE5."))
	bool IsHoloLens1Remoting = false;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Input Simulation", Meta = (DisplayName = "Enable Input Simulation", Tooltip = "Enable simulation of AR input in the editor when no HMD is connected."))
	bool bEnableInputSimulation = true;
};
