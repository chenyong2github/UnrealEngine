// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputCoreTypes.h"

namespace F4MLInputHelper
{
	UE4ML_API void CreateInputMap(TArray<TTuple<FKey, FName>>& InterfaceKeys, TMap<FKey, int32>& FKeyToInterfaceKeyMap);
}