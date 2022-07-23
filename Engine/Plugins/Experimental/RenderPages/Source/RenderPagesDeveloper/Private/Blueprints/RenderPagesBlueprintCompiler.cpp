// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/RenderPagesBlueprintCompiler.h"

#include "RenderPage/RenderPageCollection.h"
#include "Stats/StatsHierarchical.h"

#define LOCTEXT_NAMESPACE "RenderPagesBlueprintCompiler"


bool UE::RenderPages::FRenderPagesBlueprintCompiler::CanCompile(const UBlueprint* Blueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	return (Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(URenderPageCollection::StaticClass()));
}

void UE::RenderPages::FRenderPagesBlueprintCompiler::Compile(UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRenderPagesBlueprintCompilerContext Compiler(Blueprint, Results, CompileOptions);
	Compiler.Compile();
}


#undef LOCTEXT_NAMESPACE
