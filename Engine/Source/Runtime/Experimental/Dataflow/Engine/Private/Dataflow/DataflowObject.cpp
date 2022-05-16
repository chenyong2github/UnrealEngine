// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObject.h"
#include "Dataflow/Dataflow.h"
#include "Dataflow/DataflowEdNode.h"


#define LOCTEXT_NAMESPACE "UDataflow")

UDataflow::UDataflow(const FObjectInitializer& ObjectInitializer)
	: UEdGraph(ObjectInitializer)
	, Dataflow(new Dataflow::FGraph())
{}

#if WITH_EDITOR

void UDataflow::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UDataflow::PostLoad()
{
#if WITH_EDITOR
	for (UEdGraphNode* EdNode : Nodes)
	{
		UDataflowEdNode* DataflowEdNode = Cast<UDataflowEdNode>(EdNode);
		if(ensure(DataflowEdNode))
		{
			DataflowEdNode->SetDataflowGraph(Dataflow);
		}
	}
#endif

	UObject::PostLoad();
}

void UDataflow::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << *Dataflow.Get();
}

#undef LOCTEXT_NAMESPACE
