// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMMemory.h"
#include "RigVMFunction.h"
#include "RigVMTemplate.h"

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
	void Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct = nullptr, const TArray<FRigVMFunctionArgument>& InArguments = TArray<FRigVMFunctionArgument>());

	// Refreshes the list and finds the function pointers
	// based on the names.
	void Refresh();

	// Returns the function given its name (or nullptr)
	const FRigVMFunction* FindFunction(const TCHAR* InName) const;

	// Returns the function given its backing up struct and method name
	const FRigVMFunction* FindFunction(UScriptStruct* InStruct, const TCHAR* InName) const;

	// Returns all current rigvm functions
	const TChunkedArray<FRigVMFunction>& GetFunctions() const;

	// Returns a template pointer given its notation (or nullptr)
	const FRigVMTemplate* FindTemplate(const FName& InNotation) const;

	// Returns all current rigvm functions
	const TChunkedArray<FRigVMTemplate>& GetTemplates() const;

	// Defines and retrieves a template given its arguments
	const FRigVMTemplate* GetOrAddTemplateFromArguments(const FName& InName, const TArray<FRigVMTemplateArgument>& InArguments);

private:

	static const FName TemplateNameMetaName;

	// disable default constructor
	FRigVMRegistry() {}
	// disable copy constructor
	FRigVMRegistry(const FRigVMRegistry&) = delete;
	// disable assignment operator
	FRigVMRegistry& operator= (const FRigVMRegistry &InOther) = delete;

	// memory for all functions
	// We use TChunkedArray because we need the memory locations to be stable, since we only ever add and never remove.
	TChunkedArray<FRigVMFunction> Functions;

	// memory for all templates
	TChunkedArray<FRigVMTemplate> Templates;

	// name lookup for functions
	TMap<FName, int32> FunctionNameToIndex;

	// name lookup for templates
	TMap<FName, int32> TemplateNotationToIndex;

	static FRigVMRegistry s_RigVMRegistry;
	friend struct FRigVMStruct;
};
