// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPoseMirrorSettings.h"

UControlRigPoseMirrorSettings::UControlRigPoseMirrorSettings()
{
	RightSide = TEXT("_r_");
	LeftSide = TEXT("_l_");
	MirrorAxis = EAxis::X;
	AxisToFlip = EAxis::X;
}
