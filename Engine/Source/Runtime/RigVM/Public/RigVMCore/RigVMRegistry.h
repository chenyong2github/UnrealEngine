// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMFunction.h"
#include "RigVMPrototype.h"

/**
 * The FRigVMRegistry is used to manage all known function pointers
 * for use in the RigVM. The Register method is called automatically
 * when the static struct is initially constructed for each USTRUCT
 * hosting a RIGVM_METHOD enabled virtual function.
 */
struct RIGVM_API FRigVMRegistry
{
public:

	// Returns the singleton registry
	static FRigVMRegistry& Get();

	// Registers a function given its name.
	// The name will be the name of the struct and virtual method,
	// for example "FMyStruct::MyVirtualMethod"
	void Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr);

	// Refreshes the list and finds the function pointers
	// based on the names.
	void Refresh();

	// Returns a function pointer given its name
	FRigVMFunctionPtr FindFunction(const TCHAR* InName) const;

	// Returns a prototype pointer given its notation (or nullptr)
	const FRigVMPrototype* FindPrototype(const FName& InNotation) const;

	// Returns a prototype pointer given its notation (or nullptr)
	const FRigVMPrototype* FindPrototype(UScriptStruct* InStruct, const FString& InPrototypeName) const;

	// Returns registry info about a function given its name
	FRigVMFunction FindFunctionInfo(const TCHAR* InName) const;

	// Returns all current rigvm functions
	const TArray<FRigVMFunction>& GetFunctions() const;

	// Returns all current rigvm functions
	const TArray<FRigVMPrototype>& GetPrototypes() const;

private:

	static const FName PrototypeNameMetaName;

	// disable default constructor
	FRigVMRegistry() {}
	// disable copy constructor
	FRigVMRegistry(const FRigVMRegistry&) = delete;
	// disable assignment operator
	FRigVMRegistry& operator= (const FRigVMRegistry &InOther) = delete;

	// memory for all functions
	TArray<FRigVMFunction> Functions;

	// memory for all prototypes
	TArray<FRigVMPrototype> Prototypes;

	static FRigVMRegistry s_RigVMRegistry;
};
