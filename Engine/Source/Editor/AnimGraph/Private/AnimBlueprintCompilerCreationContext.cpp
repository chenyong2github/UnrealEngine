// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerCreationContext.h"
#include "AnimBlueprintCompiler.h"
#include "EdGraph/EdGraphSchema.h"

FOnStartCompilingClass& FAnimBlueprintCompilerCreationContext::OnStartCompilingClass()
{
	return CompilerContext->OnStartCompilingClassDelegate;
}

FOnPreProcessAnimationNodes& FAnimBlueprintCompilerCreationContext::OnPreProcessAnimationNodes()
{
	return CompilerContext->OnPreProcessAnimationNodesDelegate;
}

FOnPostProcessAnimationNodes& FAnimBlueprintCompilerCreationContext::OnPostProcessAnimationNodes()
{
	return CompilerContext->OnPostProcessAnimationNodesDelegate;
}

FOnPostExpansionStep& FAnimBlueprintCompilerCreationContext::OnPostExpansionStep()
{
	return CompilerContext->OnPostExpansionStepDelegate;
}

FOnFinishCompilingClass& FAnimBlueprintCompilerCreationContext::OnFinishCompilingClass()
{
	return CompilerContext->OnFinishCompilingClassDelegate;
}

FOnCopyTermDefaultsToDefaultObject& FAnimBlueprintCompilerCreationContext::OnCopyTermDefaultsToDefaultObject()
{
	return CompilerContext->OnCopyTermDefaultsToDefaultObjectDelegate;
}

void FAnimBlueprintCompilerCreationContext::RegisterKnownGraphSchema(TSubclassOf<UEdGraphSchema> InGraphSchemaClass)
{
	CompilerContext->KnownGraphSchemas.AddUnique(InGraphSchemaClass);
}

