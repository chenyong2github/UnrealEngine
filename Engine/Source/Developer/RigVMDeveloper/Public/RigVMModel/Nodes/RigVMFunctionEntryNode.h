// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMFunctionEntryNode.generated.h"

/**
 * The Function Entry node is used to provide access to the 
 * input pins of the library node for links within.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMFunctionEntryNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override node functions
	virtual bool IsDefinedAsVarying() const override;

private:

	friend class URigVMController;
};

