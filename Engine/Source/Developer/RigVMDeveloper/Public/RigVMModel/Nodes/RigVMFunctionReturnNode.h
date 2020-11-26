// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMFunctionReturnNode.generated.h"

/**
 * The Function Return node is used to provide access to the 
 * output pins of the library node for links within.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMFunctionReturnNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override node functions
	virtual bool IsDefinedAsVarying() const override;

private:

	friend class URigVMController;
};

