// Copyright Epic Games, Inc. All Rights Reserved.

#include "GauntletTestControllerBootTest.h"



void UGauntletTestControllerBootTest::OnTick(float TimeDelta)
{
	if (IsBootProcessComplete())
	{
		EndTest(0);
	}
}