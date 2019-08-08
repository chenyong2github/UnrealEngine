// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.h"
#include "RigVM.generated.h"

UCLASS()
class RIGVM_API URigVM : public UObject
{
	GENERATED_BODY()

public:

	URigVM();
	virtual ~URigVM();

	void Reset();

	bool Execute(FRigVMMemoryContainerPtrArray Memory = FRigVMMemoryContainerPtrArray(), TArrayView<void*> AdditionalArgs = TArrayView<void*>());

	UFUNCTION()
	int32 AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InFunctionName);

	UPROPERTY()
	FRigVMMemoryContainer WorkMemory;

	UPROPERTY()
	FRigVMMemoryContainer LiteralMemory;

	UPROPERTY()
	FRigVMByteCode ByteCode;

	UPROPERTY()
	FRigVMByteCodeTable Instructions;

private:

	void ResolveFunctionsIfRequired();
	void RefreshInstructionsIfRequired();

	UPROPERTY()
	TArray<FString> FunctionNames;

	TArray<FRigVMFunctionPtr> Functions;
};
