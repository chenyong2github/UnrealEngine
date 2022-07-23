// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "KismetCompilerModule.h"
#include "KismetCompiler.h"


namespace UE::RenderPages
{
	/**
	 * A IBlueprintCompiler child class for the RenderPages modules.
	 *
	 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
	 */
	class RENDERPAGESDEVELOPER_API FRenderPagesBlueprintCompiler : public IBlueprintCompiler
	{
	public:
		//~ Begin IBlueprintCompiler Interface
		virtual bool CanCompile(const UBlueprint* Blueprint) override;
		virtual void Compile(UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results) override;
		//~ End IBlueprintCompiler Interface
	};


	class RENDERPAGESDEVELOPER_API FRenderPagesBlueprintCompilerContext : public FKismetCompilerContext
	{
	public:
		FRenderPagesBlueprintCompilerContext(UBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions)
			: FKismetCompilerContext(SourceSketch, InMessageLog, InCompilerOptions)
		{}
	};
}
