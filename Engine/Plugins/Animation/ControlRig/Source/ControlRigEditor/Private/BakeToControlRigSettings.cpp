// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeToControlRigSettings.h"

UBakeToControlRigSettings::UBakeToControlRigSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bReduceKeys = false;
	Tolerance = 0.001f;
}
