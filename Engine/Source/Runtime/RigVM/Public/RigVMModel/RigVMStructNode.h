// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMNode.h"
#include "RigVMStructNode.generated.h"

UCLASS()
class RIGVM_API URigVMStructNode : public URigVMNode
{
	GENERATED_BODY()

public:

	UFUNCTION()
	UScriptStruct* GetScriptStruct() const;

	UFUNCTION()
	FName GetMethodName() const;

private:

	UPROPERTY()
	UScriptStruct* ScriptStruct;

	UPROPERTY()
	FName MethodName;

	friend class URigVMController;
};

