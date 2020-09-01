// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_AddPinInterface.h"

FName IK2Node_AddPinInterface::GetNameForAdditionalPin(int32 PinIndex)
{
	check(PinIndex < GetMaxInputPinsNum());
	const FName Name(*FString::Chr(TCHAR('A') + PinIndex));
	return Name;
}