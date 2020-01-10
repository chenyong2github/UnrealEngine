// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMStructNode.generated.h"

/**
 * The Struct Node represents a Function Invocation of a RIGVM_METHOD
 * declared on a USTRUCT. Struct Nodes have input / output pins for all
 * struct UPROPERTY members.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMStructNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Returns the UStruct for this struct node
	// (the struct declaring the RIGVM_METHOD)
	UFUNCTION(BlueprintCallable, Category = RigVMStructNode)
	UScriptStruct* GetScriptStruct() const;

	// Returns the name of the declared RIGVM_METHOD
	UFUNCTION(BlueprintCallable, Category = RigVMStructNode)
	FName GetMethodName() const;

private:

	UPROPERTY()
	UScriptStruct* ScriptStruct;

	UPROPERTY()
	FName MethodName;

	friend class URigVMController;
};

