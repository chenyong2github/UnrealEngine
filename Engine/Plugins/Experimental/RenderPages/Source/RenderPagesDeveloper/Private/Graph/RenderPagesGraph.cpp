// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RenderPagesGraph.h"
#include "Graph/RenderPagesGraphSchema.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "RenderPagesGraph"


void UDEPRECATED_RenderPagesGraph::Initialize(URenderPagesBlueprint* InBlueprint) {}

void UDEPRECATED_RenderPagesGraph::PostLoad()
{
	Super::PostLoad();
	Schema = UDEPRECATED_RenderPagesGraphSchema::StaticClass();
}

URenderPagesBlueprint* UDEPRECATED_RenderPagesGraph::GetBlueprint() const
{
	if (UDEPRECATED_RenderPagesGraph* OuterGraph = Cast<UDEPRECATED_RenderPagesGraph>(GetOuter()))
	{
		return OuterGraph->GetBlueprint();
	}
	return Cast<URenderPagesBlueprint>(GetOuter());
}


#undef LOCTEXT_NAMESPACE
