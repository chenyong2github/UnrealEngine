// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/ControlRigSettings.h"

UControlRigSettings::UControlRigSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bResetControlTransformsOnCompile(true)
#endif
{
#if WITH_EDITORONLY_DATA
	DefaultGizmoLibrary = LoadObject<UControlRigGizmoLibrary>(nullptr, TEXT("/ControlRig/Controls/DefaultGizmoLibrary.DefaultGizmoLibrary"));
	bResetControlsOnCompile = true;
	bResetControlsOnPinValueInteraction = false;
#endif
}


