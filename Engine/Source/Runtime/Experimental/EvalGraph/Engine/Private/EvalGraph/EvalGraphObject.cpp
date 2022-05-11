// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphObject.h"
#include "EvalGraph/EvalGraph.h"


#define LOCTEXT_NAMESPACE "UEvalGraph")

UEvalGraph::UEvalGraph(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer)
	, EvalGraph(new Eg::FGraph())
{}

#if WITH_EDITOR

void UEvalGraph::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UEvalGraph::PostLoad()
{
	UObject::PostLoad();
}

void UEvalGraph::Serialize(FArchive& Ar)
{
	Ar << *EvalGraph.Get();
}

#undef LOCTEXT_NAMESPACE
