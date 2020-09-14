// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementSelectionInterface.h"
#include "ComponentElementSelectionInterface.generated.h"

class UTypedElementList;

UCLASS()
class ENGINE_API UComponentElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual UObject* Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle) override;

	static int32 GetNumSelectedComponents(const UTypedElementList* InCurrentSelection);
	static bool HasSelectedComponents(const UTypedElementList* InCurrentSelection);
};
