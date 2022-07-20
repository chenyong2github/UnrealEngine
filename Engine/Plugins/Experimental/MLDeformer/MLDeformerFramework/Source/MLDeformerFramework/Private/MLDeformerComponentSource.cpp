// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerComponentSource.h"

#include "MLDeformerComponent.h"


#define LOCTEXT_NAMESPACE "MLDeformerComponentSource"


FName UMLDeformerComponentSource::Contexts::Vertex("Vertex");


FText UMLDeformerComponentSource::GetDisplayName() const
{
	return LOCTEXT("MLDeformerComponent", "ML Deformer Component");
}


TSubclassOf<UActorComponent> UMLDeformerComponentSource::GetComponentClass() const
{
	return UMLDeformerComponent::StaticClass();
}


TArray<FName> UMLDeformerComponentSource::GetExecutionContexts() const
{
	return {Contexts::Vertex};
}


#undef LOCTEXT_NAMESPACE
