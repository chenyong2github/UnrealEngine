// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "K2Node_AddPinInterface.generated.h"

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class BLUEPRINTGRAPH_API UK2Node_AddPinInterface : public UInterface
{
	GENERATED_BODY()
};

class UEdGraphPin;

/**
* Interface for adding the small "Add Pin" symbol to a node in the bottom right hand side. 
* Implementing this interface will provide the API needed to get the UI up and running, but 
* the actual pin creation/naming is up to the specific node. 
* 
* @see UK2Node_CommutativeAssociativeBinaryOperator, UK2Node_DoOnceMultiInput
*/
class BLUEPRINTGRAPH_API IK2Node_AddPinInterface
{
	GENERATED_BODY()

public:

	static constexpr int32 GetMaxInputPinsNum()
	{
		return (TCHAR('Z') - TCHAR('A'));
	}

	static FName GetNameForAdditionalPin(int32 PinIndex);

	virtual void AddInputPin() { }
	virtual bool CanAddPin() const { return true; }

	virtual void RemoveInputPin(UEdGraphPin* Pin) { } 

protected:

	virtual bool CanRemovePin(const UEdGraphPin* Pin) const { return true; };
};
