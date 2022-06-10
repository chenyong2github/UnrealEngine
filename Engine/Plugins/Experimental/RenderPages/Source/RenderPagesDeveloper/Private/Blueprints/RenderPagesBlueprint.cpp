// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/RenderPagesBlueprint.h"
#include "Graph/RenderPagesGraph.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPagesBlueprintGeneratedClass.h"


URenderPagesBlueprint::URenderPagesBlueprint()
{
	CompileLog.SetSourcePath(GetPathName());
	CompileLog.bLogDetailedResults = false;
	CompileLog.EventDisplayThresholdMs = false;
}

UClass* URenderPagesBlueprint::GetBlueprintClass() const
{
	return URenderPagesBlueprintGeneratedClass::StaticClass();
}

void URenderPagesBlueprint::PostLoad()
{
	Super::PostLoad();

	{
		// Remove all non Render Pages graphs
		TArray<UEdGraph*> NewUberGraphPages;

		for (UEdGraph* Graph : UbergraphPages)
		{
			if (URenderPagesGraph* RenderPagesGraph = Cast<URenderPagesGraph>(Graph))
			{
				NewUberGraphPages.Add(RenderPagesGraph);
			}
			else
			{
				Graph->MarkAsGarbage();
				Graph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
			}
		}

		UbergraphPages = NewUberGraphPages;
	}

	CompileLog.Messages.Reset();
	CompileLog.NumErrors = CompileLog.NumWarnings = 0;
}
