// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeveloperToolSettingsDelegates.h"
#include "Modules/ModuleManager.h"


FDeveloperToolSettingsDelegates::FOnNativeBlueprintsSettingChanged FDeveloperToolSettingsDelegates::OnNativeBlueprintsSettingChanged;


IMPLEMENT_MODULE(FDefaultModuleImpl, DeveloperToolSettings);
