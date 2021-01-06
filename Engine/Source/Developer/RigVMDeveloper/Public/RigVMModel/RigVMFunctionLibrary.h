// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMGraph.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMFunctionLibrary.generated.h"

/**
 * The Function Library is a graph used only to store
 * the sub graphs used for functions.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMFunctionLibrary : public URigVMGraph
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMFunctionLibrary();

	// Returns all of the stored functions
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	TArray<URigVMLibraryNode*> GetFunctions() const;

	// Finds a function by name
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	URigVMLibraryNode* FindFunction(const FName& InFunctionName) const;

private:

	friend class URigVMController;
	friend class URigVMCompiler;
};

