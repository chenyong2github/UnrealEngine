// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementSelectionInterface.h"
#include "ActorElementSelectionInterface.generated.h"

UCLASS()
class UActorElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool IsValidSelection(const FTypedElementHandle& InElementHandle) override;
	virtual UObject* Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle) override;
};
