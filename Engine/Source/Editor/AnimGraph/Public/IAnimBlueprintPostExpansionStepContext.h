// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"

class FCompilerResultsLog;
class UEdGraph;
struct FKismetCompilerOptions;

// Interface passed to PostExpansionStep delegate
class IAnimBlueprintPostExpansionStepContext
{
public:
	virtual ~IAnimBlueprintPostExpansionStepContext() {}

	// Get a handler of the specified type and name (i.e. via simple name-based RTTI)
	// Handlers are registered via IAnimBlueprintCompilerHandlerCollection::RegisterHandler
	template <typename THandlerClass>
	THandlerClass* GetHandler(FName InName) const
	{
		return static_cast<THandlerClass*>(GetHandlerInternal(InName));
	}

	// Get the message log for the current compilation
	FCompilerResultsLog& GetMessageLog() const { return GetMessageLogImpl(); }

	// Get the consolidated uber graph during compilation
	UEdGraph* GetConsolidatedEventGraph() const { return GetConsolidatedEventGraphImpl(); }

	// Get the compiler options we are currently using
	const FKismetCompilerOptions& GetCompileOptions() const { return GetCompileOptionsImpl(); }

protected:
	// Get the message log for the current compilation
	virtual FCompilerResultsLog& GetMessageLogImpl() const = 0;

	// Get the consolidated uber graph during compilation
	virtual UEdGraph* GetConsolidatedEventGraphImpl() const = 0;

	// Get the compiler options we are currently using
	virtual const FKismetCompilerOptions& GetCompileOptionsImpl() const = 0;

	// GetHandler helper function
	virtual IAnimBlueprintCompilerHandler* GetHandlerInternal(FName InName) const = 0;
};
