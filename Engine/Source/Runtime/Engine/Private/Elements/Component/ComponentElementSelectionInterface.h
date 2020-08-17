// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementSelectionInterface.h"
#include "ComponentElementSelectionInterface.generated.h"

UCLASS()
class UComponentElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool IsValidSelection(const FTypedElementHandle& InElementHandle) override;
	virtual UObject* Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle) override;
};
