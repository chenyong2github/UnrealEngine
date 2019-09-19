// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UnrealHeaderToolStats.h"

FUnrealHeaderToolStats GUnrealHeaderToolStats;

FUnrealHeaderToolStats& FUnrealHeaderToolStats::Get()
{
	return GUnrealHeaderToolStats;
}

double& FUnrealHeaderToolStats::GetCounter(const FName& Key)
{
	return GUnrealHeaderToolStats.Counters.FindOrAdd(Key);
}
