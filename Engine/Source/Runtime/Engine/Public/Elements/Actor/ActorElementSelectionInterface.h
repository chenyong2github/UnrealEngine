// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "ActorElementSelectionInterface.generated.h"

UCLASS()
class ENGINE_API UActorElementSelectionInterface : public UTypedElementSelectionInterface
{
	GENERATED_BODY()

public:
	static int32 GetNumSelectedActors(const UTypedElementList* InCurrentSelection);
	static bool HasSelectedActors(const UTypedElementList* InCurrentSelection);
};
