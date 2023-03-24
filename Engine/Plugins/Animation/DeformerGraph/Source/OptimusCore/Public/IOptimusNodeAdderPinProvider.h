// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "OptimusNode.h"

#include "IOptimusNodeAdderPinProvider.generated.h"

UINTERFACE()
class OPTIMUSCORE_API UOptimusNodeAdderPinProvider :
	public UInterface
{
	GENERATED_BODY()
};

/**
* Interface that provides a mechanism to query information about parameter bindings
*/
class OPTIMUSCORE_API IOptimusNodeAdderPinProvider
{
	GENERATED_BODY()

public:
	virtual bool CanAddPinFromPin(
		const UOptimusNodePin* InSourcePin,
		EOptimusNodePinDirection InNewPinDirection,
		FString* OutReason = nullptr
		) const = 0;

	// If there are multiple options for the target parent pin,
	// a menu should show up for the user to choose one from these options
	virtual TArray<UOptimusNodePin*> GetTargetParentPins(const UOptimusNodePin* InSourcePin) const = 0;
	
	virtual TArray<UOptimusNodePin*> TryAddPinFromPin(
		UOptimusNodePin* InPreferredTargetParentPin,
		UOptimusNodePin* InSourcePin
		) = 0;
	
	virtual bool RemoveAddedPins(TConstArrayView<UOptimusNodePin*> InAddedPinsToRemove) = 0;

	virtual FName GetSanitizedNewPinName(UOptimusNodePin* InTargetParentPin, FName InPinName)= 0;
};
