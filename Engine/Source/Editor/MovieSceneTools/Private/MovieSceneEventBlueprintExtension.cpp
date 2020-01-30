// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneEventBlueprintExtension.h"
#include "MovieSceneEventUtils.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "K2Node_FunctionEntry.h"
#include "KismetCompiler.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"

void UMovieSceneEventBlueprintExtension::PostLoad()
{
	EventSections.Remove(nullptr);
	Super::PostLoad();
}

void UMovieSceneEventBlueprintExtension::HandlePreloadObjectsForCompilation(UBlueprint* OwningBlueprint)
{
	for (UMovieSceneEventSectionBase* EventSection : EventSections)
	{
		if (EventSection)
		{
			UBlueprint::ForceLoad(EventSection);
		}
	}
}

void UMovieSceneEventBlueprintExtension::HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext)
{
	for (UMovieSceneEventSectionBase* EventSection : EventSections)
	{
		if (!EventSection)
		{
			continue;
		}

		ensureMsgf(!EventSection->HasAnyFlags(RF_NeedLoad), TEXT("Attempting to generate entry point functions before an event section has been loaded"));

		EventSection->AttemptUpgrade();

		for (FMovieSceneEvent& EntryPoint : EventSection->GetAllEntryPoints())
		{
			UEdGraphNode* Endpoint = FMovieSceneEventUtils::FindEndpoint(&EntryPoint, EventSection, CompilerContext->Blueprint);
			if (Endpoint)
			{
				UK2Node_FunctionEntry* FunctionEntry = FMovieSceneEventUtils::GenerateEntryPoint(EventSection, &EntryPoint, CompilerContext, Endpoint);
				if (FunctionEntry)
				{
					EntryPoint.CompiledFunctionName = FunctionEntry->GetGraph()->GetFName();
				}
				else
				{
					EntryPoint.CompiledFunctionName = NAME_None;
				}
			}
		}


		auto OnFunctionListGenerated = [WeakEventSection = MakeWeakObjectPtr(EventSection)](FKismetCompilerContext* CompilerContext)
		{
			UMovieSceneEventSectionBase* PinnedSection = WeakEventSection.Get();
			if (ensureMsgf(PinnedSection, TEXT("Event section has been collected during blueprint compilation.")))
			{
				PinnedSection->OnPostCompile(CompilerContext->Blueprint);
			}
		};

		CompilerContext->OnFunctionListCompiled().AddLambda(OnFunctionListGenerated);
	}
}