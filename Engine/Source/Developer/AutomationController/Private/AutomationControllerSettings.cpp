// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "AutomationControllerSettings.h"


UAutomationControllerSettings::UAutomationControllerSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bTreatLogErrorsAsTestErrors(true)
	, bTreatLogWarningsAsTestErrors(false)
{
}
