// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "ComponentElementSelectionInterface.generated.h"

UCLASS()
class ENGINE_API UComponentElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	static int32 GetNumSelectedComponents(const UTypedElementList* InCurrentSelection);
	static bool HasSelectedComponents(const UTypedElementList* InCurrentSelection);
};
