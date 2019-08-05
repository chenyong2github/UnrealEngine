// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMStorage.h"

typedef void (*FRigVMFunctionPtr)(const TArrayView<FRigVMArgument>&, FRigVMStorage**, const TArrayView<void*>&);

struct RIGVM_API FRigVMFunction
{
	const TCHAR* Name;
	FRigVMFunctionPtr FunctionPtr;

	FRigVMFunction()
		: Name(nullptr)
		, FunctionPtr(nullptr)
	{
	}

	FRigVMFunction(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr)
		: Name(InName)
		, FunctionPtr(InFunctionPtr)
	{
	}
};

struct RIGVM_API FRigVMRegistry
{
public:
	static FRigVMRegistry& Get();

	void Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr);
	void Refresh();

	FRigVMFunctionPtr Find(const TCHAR* InName) const;

private:

	// disable default constructor
	FRigVMRegistry() {}
	// disable copy constructor
	FRigVMRegistry(const FRigVMRegistry&) {}
	// disable assignment operator
	FRigVMRegistry& operator= (const FRigVMRegistry &InOther) { return *this; }

	// storage for all functions
	TArray<FRigVMFunction> Functions;

	static FRigVMRegistry s_RigVMRegistry;
};
