// Copyright Epic Games, Inc. All Rights Reserved.


#include "AutomationControllerSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogAutomationControllerSettings, Log, All)

UAutomationControllerSettings::UAutomationControllerSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bSuppressLogErrors(false)
	, bSuppressLogWarnings(false)
	, bElevateLogWarningsToErrors(true)
	, bTreatLogWarningsAsTestErrors(true)
	, GameInstanceLostTimerSeconds(300.0f)
	, bResetTelemetryStorageOnNewSession(false)
{
}

void UAutomationControllerSettings::PostInitProperties()
{
	Super::PostInitProperties();

	if (!bTreatLogWarningsAsTestErrors)
	{
		UE_LOG(LogAutomationControllerSettings, Warning, TEXT("UAutomationControllerSettings::bTreatLogWarningsAsTestErrors is deprecated. Use bElevateLogWarningsToErrors instead."));
		bElevateLogWarningsToErrors = false;
	}
}