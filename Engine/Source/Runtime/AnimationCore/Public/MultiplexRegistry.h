// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiplexStorage.h"

struct ANIMATIONCORE_API FMultiplexArgument
{
public:

	FMultiplexArgument()
		:Address(INDEX_NONE)
	{
	}

	FMultiplexArgument(int32 InAddress)
		:Address(InAddress)
	{
	}

	FORCEINLINE bool operator ==(int32 InOther) const { return Address == InOther; }

	FORCEINLINE bool IsLiteral() const { return Address < 0; }
	FORCEINLINE int32 StorageType() const { return Address < 0 ? 1 : 0; }
	FORCEINLINE int32 Index() const { return Address < 0 ? -(Address + 1) : Address; }

private:
	int32 Address;
};

typedef void (*FMultiplexFunctionPtr)(const TArrayView<FMultiplexArgument>&, FMultiplexStorage*, const TArrayView<void*>&);

struct ANIMATIONCORE_API FMultiplexFunction
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

struct ANIMATIONCORE_API FMultiplexRegistry
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
