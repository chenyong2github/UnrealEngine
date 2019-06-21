// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericAccessibleInterfaces.h"

DEFINE_LOG_CATEGORY(LogAccessibility);

#if WITH_ACCESSIBILITY
#include "HAL/IConsoleManager.h"

/** A flag that can be set by a user to force accessibility off regardless of other settings. */
bool GAllowAccessibility = false;
FAutoConsoleVariableRef AllowAccessibilityRef(
	TEXT("Accessibility.Enable"),
	GAllowAccessibility,
	TEXT("If false, all queries from accessible APIs will be ignored. On some platforms, the application must be restarted in order to take effect.")
);

bool FGenericAccessibleMessageHandler::ApplicationIsAccessible() const
{
	return bApplicationIsAccessible && GAllowAccessibility;
}

void FGenericAccessibleMessageHandler::SetActive(bool bActive)
{
	bActive &= GAllowAccessibility;
	if (bActive != bIsActive)
	{
		bIsActive = bActive;

		if (bIsActive)
		{
			UE_LOG(LogAccessibility, Verbose, TEXT("Enabling Accessibility"));
			OnActivate();
		}
		else
		{
			OnDeactivate();
			UE_LOG(LogAccessibility, Verbose, TEXT("Accessibility Disabled"));
		}
	}
}
#endif
