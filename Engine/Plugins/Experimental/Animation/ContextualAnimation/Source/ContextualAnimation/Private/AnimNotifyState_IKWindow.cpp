// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNotifyState_IKWindow.h"

UAnimNotifyState_IKWindow::UAnimNotifyState_IKWindow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BlendIn.SetBlendTime(0.25f);
	BlendOut.SetBlendTime(0.25f);
}

FString UAnimNotifyState_IKWindow::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("IK (%s)"), *GoalName.ToString());
}