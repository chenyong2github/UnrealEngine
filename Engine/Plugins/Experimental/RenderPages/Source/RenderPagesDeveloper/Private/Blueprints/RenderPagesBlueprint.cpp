// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/RenderPagesBlueprint.h"

#include "EdGraphSchema_K2_Actions.h"
#include "Graph/RenderPagesGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPagesBlueprintGeneratedClass.h"


UClass* URenderPagesBlueprint::GetBlueprintClass() const
{
	return URenderPagesBlueprintGeneratedClass::StaticClass();
}

void URenderPagesBlueprint::PostLoad()
{
	Super::PostLoad();

	{// remove every URenderPagesGraph (because that class is deprecated) >>
		bool bChanged = false;
		TArray<TObjectPtr<UEdGraph>> NewUberGraphPages;

		for (const TObjectPtr<UEdGraph>& Graph : UbergraphPages)
		{
			if (UDEPRECATED_RenderPagesGraph* RenderPagesGraph = Cast<UDEPRECATED_RenderPagesGraph>(Graph))
			{
				bChanged = true;
				RenderPagesGraph->MarkAsGarbage();
				RenderPagesGraph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
				continue;
			}
			NewUberGraphPages.Add(Graph);
		}

		if (bChanged)
		{
			UbergraphPages = NewUberGraphPages;
		}
	}// remove every URenderPagesGraph (because that class is deprecated) <<

	if (UbergraphPages.IsEmpty())
	{
		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(this, UEdGraphSchema_K2::GN_EventGraph, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		NewGraph->bAllowDeletion = false;

		{// create every RenderPages blueprint event >>
			int32 i = 0;
			for (const FString& Event : URenderPageCollection::GetBlueprintImplementableEvents())
			{
				int32 InOutNodePosY = (i * 256) - 48;
				FKismetEditorUtilities::AddDefaultEventNode(this, NewGraph, FName(Event), URenderPageCollection::StaticClass(), InOutNodePosY);
				i++;
			}
		}// create every RenderPages blueprint event <<

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
		FBlueprintEditorUtils::AddUbergraphPage(this, NewGraph);
		LastEditedDocuments.AddUnique(NewGraph);
	}
}
