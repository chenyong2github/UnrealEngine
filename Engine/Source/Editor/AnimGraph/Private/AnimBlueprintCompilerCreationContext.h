// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerCreationContext.h"
#include "Templates/SubclassOf.h"

class FAnimBlueprintCompilerContext;

class FAnimBlueprintCompilerCreationContext : public IAnimBlueprintCompilerCreationContext
{
private:
	friend class FAnimBlueprintCompilerHandlerCollection;

	FAnimBlueprintCompilerCreationContext(FAnimBlueprintCompilerContext* InCompilerContext)
		: CompilerContext(InCompilerContext)
	{}

	// IAnimBlueprintCompilerCreationContext interface
	virtual FOnStartCompilingClass& OnStartCompilingClass() override;
	virtual FOnPreProcessAnimationNodes& OnPreProcessAnimationNodes() override;
	virtual FOnPostProcessAnimationNodes& OnPostProcessAnimationNodes() override;
	virtual FOnPostExpansionStep& OnPostExpansionStep() override;
	virtual FOnFinishCompilingClass& OnFinishCompilingClass() override;
	virtual FOnCopyTermDefaultsToDefaultObject& OnCopyTermDefaultsToDefaultObject() override;
	virtual void RegisterKnownGraphSchema(TSubclassOf<UEdGraphSchema> InGraphSchemaClass) override;

private:
	FAnimBlueprintCompilerContext* CompilerContext;
};