// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiplexStorage.h"

typedef void (*FMultiplexFunctionPtr)(const TArrayView<FMultiplexArgument>&, FMultiplexStorage**, const TArrayView<void*>&);

struct RIGVM_API FMultiplexFunction
{
	const TCHAR* Name;
	FMultiplexFunctionPtr FunctionPtr;

	FMultiplexFunction()
		: Name(nullptr)
		, FunctionPtr(nullptr)
	{
	}

	FMultiplexFunction(const TCHAR* InName, FMultiplexFunctionPtr InFunctionPtr)
		: Name(InName)
		, FunctionPtr(InFunctionPtr)
	{
	}
};

struct RIGVM_API FMultiplexRegistry
{
public:
	static FMultiplexRegistry& Get();

	void Register(const TCHAR* InName, FMultiplexFunctionPtr InFunctionPtr);
	void Refresh();

	FMultiplexFunctionPtr Find(const TCHAR* InName) const;

private:

	// disable default constructor
	FMultiplexRegistry() {}
	// disable copy constructor
	FMultiplexRegistry(const FMultiplexRegistry&) {}
	// disable assignment operator
	FMultiplexRegistry& operator= (const FMultiplexRegistry &InOther) { return *this; }

	// storage for all functions
	TArray<FMultiplexFunction> Functions;

	static FMultiplexRegistry s_MultiplexRegistry;
};
