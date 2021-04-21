// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"
#include "IAnimBlueprintCompilerHandlerCollection.h"

class FAnimBlueprintCompilerContext;

/** Handler collection for the anim blueprint compiler */
class FAnimBlueprintCompilerHandlerCollection : public IAnimBlueprintCompilerHandlerCollection
{
private:
	friend class FAnimBlueprintCompilerContext;

	void Initialize(FAnimBlueprintCompilerContext* InCompilerContext);

	/** Get a named handler */
	virtual IAnimBlueprintCompilerHandler* GetHandlerByName(FName InName) const override;

	/** All the currently constructed handlers */
	TMap<FName, TUniquePtr<IAnimBlueprintCompilerHandler>> Handlers;
};