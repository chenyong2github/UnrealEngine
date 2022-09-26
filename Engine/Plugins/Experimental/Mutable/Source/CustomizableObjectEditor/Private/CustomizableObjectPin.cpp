// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectPin.h"


bool IsPinOrphan(const UEdGraphPin &Pin)
{
	return Pin.bOrphanedPin;
}


void OrphanPin(UEdGraphPin& Pin)
{
	Pin.bOrphanedPin = true;
	Pin.SetSavePinIfOrphaned(true);
}