// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/ControlRigSettings.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMNode.h"
#endif

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
	bEnableUndoForPoseInteraction = true;

	SetupEventBorderColor = FLinearColor::Red;
	BackwardsSolveBorderColor = FLinearColor::Yellow;
	BackwardsAndForwardsBorderColor = FLinearColor::Blue;

#endif
}
