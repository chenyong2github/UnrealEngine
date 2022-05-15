// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObject.h"
#include "Dataflow/Dataflow.h"


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
	UObject::PostLoad();
}

void UDataflow::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << *Dataflow.Get();
}

#undef LOCTEXT_NAMESPACE
