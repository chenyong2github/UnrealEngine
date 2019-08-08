// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.h"
#include "RigVM.generated.h"

/**
 * The RigVM is the main object for evaluating FRigVMByteCode instructions.
 * It combines the byte code, a list of required function pointers for 
 * execute instructions and required memory in one class.
 */
UCLASS()
class RIGVM_API URigVM : public UObject
{
	GENERATED_BODY()

public:

	URigVM();
	virtual ~URigVM();

	// resets the container and removes all memory
	void Reset();

	// Executes the VM.
	// You can optionally provide external memory to the execution
	// and provide optional additional arguments.
	bool Execute(FRigVMMemoryContainerPtrArray Memory = FRigVMMemoryContainerPtrArray(), TArrayView<void*> AdditionalArgs = TArrayView<void*>());

	// Add a function for execute instructions to this VM.
	// Execute instructions can then refer to the function by index.
	UFUNCTION()
	int32 AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InMethodName);

	// The default mutable work memory
	UPROPERTY()
	FRigVMMemoryContainer WorkMemory;

	// The default const literal memory
	UPROPERTY()
	FRigVMMemoryContainer LiteralMemory;

	// The byte code of the VM
	UPROPERTY()
	FRigVMByteCode ByteCode;

	// Returns the instructions of the VM
	const FRigVMInstructionArray& GetInstructions();
	
private:

	void ResolveFunctionsIfRequired();
	void RefreshInstructionsIfRequired();

	UPROPERTY(transient)
	FRigVMInstructionArray Instructions;

	UPROPERTY()
	TArray<FString> FunctionNames;

	TArray<FRigVMFunctionPtr> Functions;
};
