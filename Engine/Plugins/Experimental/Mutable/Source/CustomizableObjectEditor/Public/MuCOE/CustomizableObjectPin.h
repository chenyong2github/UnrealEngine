// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphPin.h"


/** Returns true if the pin is orphan. */
bool IsPinOrphan(const UEdGraphPin &Pin);


/** Allows to perform work when orphaning a pin. */
void OrphanPin(UEdGraphPin& Pin);
