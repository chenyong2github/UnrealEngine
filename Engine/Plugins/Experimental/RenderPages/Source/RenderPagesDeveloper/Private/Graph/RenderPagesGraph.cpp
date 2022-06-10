// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RenderPagesGraph.h"
#include "Graph/RenderPagesGraphSchema.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "RenderPagesGraph"


void URenderPagesGraph::Initialize(URenderPagesBlueprint* InBlueprint) {}

void URenderPagesGraph::PostLoad()
{
	Super::PostLoad();
	Schema = URenderPagesGraphSchema::StaticClass();
}

URenderPagesBlueprint* URenderPagesGraph::GetBlueprint() const
{
	if (URenderPagesGraph* OuterGraph = Cast<URenderPagesGraph>(GetOuter()))
	{
		return OuterGraph->GetBlueprint();
	}
	return Cast<URenderPagesBlueprint>(GetOuter());
}


#undef LOCTEXT_NAMESPACE
