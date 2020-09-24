// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeToControlRigSettings.h"

UBakeToControlRigSettings::UBakeToControlRigSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bReduceKeys = true;
	Tolerance = 0.1;
}
