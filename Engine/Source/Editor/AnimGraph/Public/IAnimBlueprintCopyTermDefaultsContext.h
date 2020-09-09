// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FCompilerResultsLog;
class UAnimBlueprint;

// Interface passed to CopyTermDefaults delegate
class IAnimBlueprintCopyTermDefaultsContext
{
public:	
	virtual ~IAnimBlueprintCopyTermDefaultsContext() {}

	// Get the message log for the current compilation
	FCompilerResultsLog& GetMessageLog() const { return GetMessageLogImpl(); }

	// Get the currently-compiled anim blueprint
	const UAnimBlueprint* GetAnimBlueprint() const { return GetAnimBlueprintImpl(); }

protected:
	// Get the message log for the current compilation
	virtual FCompilerResultsLog& GetMessageLogImpl() const = 0;

	// Get the currently-compiled anim blueprint
	virtual const UAnimBlueprint* GetAnimBlueprintImpl() const = 0;
};
