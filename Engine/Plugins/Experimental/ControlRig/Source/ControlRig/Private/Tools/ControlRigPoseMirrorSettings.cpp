// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPoseMirrorSettings.h"

UControlRigPoseMirrorSettings::UControlRigPoseMirrorSettings()
{
	RightSide = TEXT("Right");
	LeftSide = TEXT("Left");
	MirrorAxis = EAxis::X;
	AxisToFlip = EAxis::Z;
}
