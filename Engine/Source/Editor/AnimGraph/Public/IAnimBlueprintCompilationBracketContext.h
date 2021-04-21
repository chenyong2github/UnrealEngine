// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FCompilerResultsLog;
class IAnimBlueprintCompilerHandler;

// Interface passed to start/end compilation delegates
class ANIMGRAPH_API IAnimBlueprintCompilationBracketContext
{
public:	
	virtual ~IAnimBlueprintCompilationBracketContext() {}

	// Get a handler of the specified type and name (i.e. via simple name-based RTTI)
	// Handlers are registered via IAnimBlueprintCompilerHandlerCollection::RegisterHandler
	template <typename THandlerClass>
	THandlerClass* GetHandler(FName InName) const
	{
		return static_cast<THandlerClass*>(GetHandlerInternal(InName));
	}

	// Get the message log for the current compilation
	FCompilerResultsLog& GetMessageLog() const { return GetMessageLogImpl(); }

protected:	
	// GetHandler helper function
	virtual IAnimBlueprintCompilerHandler* GetHandlerInternal(FName InName) const = 0;

	// Get the message log for the current compilation
	virtual FCompilerResultsLog& GetMessageLogImpl() const = 0;
};
