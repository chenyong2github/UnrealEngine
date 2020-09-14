// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementSelectionInterface.h"
#include "ActorElementSelectionInterface.generated.h"

class UTypedElementList;

UCLASS()
class ENGINE_API UActorElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual UObject* Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle) override;

	static int32 GetNumSelectedActors(const UTypedElementList* InCurrentSelection);
	static bool HasSelectedActors(const UTypedElementList* InCurrentSelection);
};
