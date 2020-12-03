// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
  * FDeveloperToolSettingsDelegates
  * Delegates that are needed for developer tools to talk to engine/editor
  **/
struct DEVELOPERTOOLSETTINGS_API FDeveloperToolSettingsDelegates
{
	/** Sent after a nativization setting changes */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNativeBlueprintsSettingChanged, const FString& PackageName, bool bSelect);
	static FOnNativeBlueprintsSettingChanged OnNativeBlueprintsSettingChanged;
};

