// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "SMInstanceElementSelectionInterface.generated.h"

UCLASS()
class ENGINE_API USMInstanceElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	static int32 GetNumSelectedSMInstances(const UTypedElementList* InCurrentSelection);
	static bool HasSelectedSMInstances(const UTypedElementList* InCurrentSelection);
};
