// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameplayTagContainer.h"
#include "Misc/FrameRate.h"

#include "StageMonitoringSettings.generated.h"


/**
 * Settings related to FramePerformance messages
 */
USTRUCT()
struct STAGEDATACORE_API FStageFramePerformanceSettings
{
	GENERATED_BODY()

public:

	/** If true, Frame Performance messages will only be sent on machine with role contained in SupportedRoles */
	UPROPERTY(config, EditAnywhere, Category = "Frame Performance", meta = (InlineEditConditionToggle))
	bool bUseRoleFiltering = false;

	/** Roles (at least one) required for Frame Performance messages to be sent if bUseRoleFiltering is true */
	UPROPERTY(config, EditAnywhere, Category = "Frame Performance", meta = (EditCondition = "bUseRoleFiltering"))
	FGameplayTagContainer SupportedRoles;

	/** Target FPS we're aiming for.  */
	UPROPERTY(config, EditAnywhere, Category = "Frame Performance", meta = (Unit = "s"))
	float UpdateInterval;
};

/**
 * Settings related to HitchDetection messages
 */
USTRUCT()
struct STAGEDATACORE_API FStageHitchDetectionSettings
{
	GENERATED_BODY()

public:

	/** If true, Hitch events will only be sent on machine with role contained in SupportedRoles */
	UPROPERTY(config, EditAnywhere, Category = "Hitch Detection", meta = (InlineEditConditionToggle))
	bool bUseRoleFiltering = false;

	/** Roles (at least one) required for Hitch Events to be sent if bUseRoleFiltering is true */
	UPROPERTY(config, EditAnywhere, Category = "Hitch Detection", meta = (EditCondition = "bUseRoleFiltering"))
	FGameplayTagContainer SupportedRoles;

	/** Target FPS we're aiming for.  */
	UPROPERTY(config, EditAnywhere, Category = "Hitch Detection")
	FFrameRate TargetFrameRate = FFrameRate(24, 1);
};

/**
 * Settings associated to DataProviders
 */
USTRUCT()
struct STAGEDATACORE_API FStageDataProviderSettings
{
	GENERATED_BODY()

public:

	/** If true, DataProvider will only start if machine has a role contained in SupportedRoles */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bUseRoleFiltering = false;

	/** If checked, VP Role of this instance must be part of these roles to have the data provider operational */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (EditCondition = "bUseRoleFiltering"))
	FGameplayTagContainer SupportedRoles;

	
	/** Settings about Frame Performance messaging */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	FStageFramePerformanceSettings FramePerformanceSettings;

	/** Settings about Hitch detection*/
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	FStageHitchDetectionSettings HitchDetectionSettings;
};

/**
 * Settings for StageMonitor
 */
USTRUCT()
struct STAGEDATACORE_API FStageMonitorSettings
{
	GENERATED_BODY()

public:
	FStageMonitorSettings();

	/** Returns true if Monitor should start on launch. Can be overriden through commandline */
	bool ShouldAutoStartOnLaunch() const;

public:
	/** If true, Monitor will only start if machine has a role contained in SupportedRoles */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (InlineEditConditionToggle))
	bool bUseRoleFiltering = false;

	/** If checked, VP Role of this instance must be part of these roles to have the monitor operational */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (EditCondition = "bUseRoleFiltering"))
	FGameplayTagContainer SupportedRoles;

	/** Interval between each discovery signal sent by Monitors */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (Units = "s"))
	float DiscoveryMessageInterval = 2.0f;

protected:
	/**
	 * Whether we should start monitoring on launch.
	 * @note It may be overriden via the command line, "-StageMonitorAutoStart=1 and via command line in editor"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	bool bAutoStart = false;

	/**
	 * Override AutoStart project settings through command line.
	 * ie. "-StageMonitorAutoStart=1"
	 */
	TOptional<bool> bCommandLineAutoStart;
};

/**
 * Settings for the StageMonitoring plugin modules. 
 * Data Provider, Monitor and shared settings are contained here to centralize access through project settings
 */
UCLASS(config=Game)
class STAGEDATACORE_API UStageMonitoringSettings : public UObject
{
	GENERATED_BODY()

public:
	UStageMonitoringSettings();

	/** Returns current SessionId either based on settings or overriden by commandline */
	int32 GetStageSessionId() const;

public:

	/** If true, Stage monitor will only listen to Stage Providers with same sessionId */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	bool bUseSessionId = true;

protected:
	/**
	 * The projects Stage SessionId to differentiate data sent over network.
	 * @note It may be overriden via the command line, "-StageSessionId=1"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	int32 StageSessionId = INDEX_NONE;

public:

	/** Interval threshold between message reception before dropping out a provider or a monitor */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta=(Unit="s"))
	float TimeoutInterval = 10.0f;

	/** Settings for monitors */
	UPROPERTY(config, EditAnywhere, Category = "Monitor Settings")
	FStageMonitorSettings MonitorSettings;

	/** Settings for Data Providers */
	UPROPERTY(config, EditAnywhere, Category = "Provider Settings")
	FStageDataProviderSettings ProviderSettings;

	/**
	 * The current SessionId in a virtual production context read from the command line.
	 * ie. "-StageSessionId=1"
	 */
	TOptional<int32> CommandLineSessionId;

	/**
	 * A friendly name for that instance given through command line (-StageFriendlyName=) to identify it when monitoring.
	 * If none, by default it will be filled with MachineName:ProcessId
	 */
	FName CommandLineFriendlyName;
};