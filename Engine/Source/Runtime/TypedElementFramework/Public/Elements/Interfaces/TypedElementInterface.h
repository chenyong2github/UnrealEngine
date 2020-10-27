// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TypedElementInterface.generated.h"

/**
 * Base typed used to represent element interfaces.
 * @note Top-level element interfaces that inherit from this should also specialize TTypedElement for their API.
 */
UCLASS(Abstract)
class TYPEDELEMENTFRAMEWORK_API UTypedElementInterface : public UObject
{
	GENERATED_BODY()
};
