// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"

#include "MetasoundArchetypeRegistration.h"

UMetasound::UMetasound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(RootMetasoundDocument)
{
}

#if WITH_EDITORONLY_DATA
UEdGraph* UMetasound::GetGraph()
{
	return Graph;
}

const UEdGraph* UMetasound::GetGraph() const
{
	return Graph;
}

UEdGraph& UMetasound::GetGraphChecked()
{
	check(Graph);
	return *Graph;
}

const UEdGraph& UMetasound::GetGraphChecked() const
{
	check(Graph);
	return *Graph;
}

void UMetasound::SetGraph(UEdGraph* InGraph)
{
	Graph = InGraph;
}
#endif // WITH_EDITORONLY_DATA

// This can be used to update the metadata (name, author, etc) for this metasound.
// @param InMetadata may be updated with any corrections we do to the input metadata.
void UMetasound::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	FMetasoundAssetBase::SetMetadata(InMetadata);
	MarkPackageDirty();
}

void UMetasound::PostLoad()
{
	ConformDocumentToArchetype();
	Super::PostLoad();
}

FMetasoundArchetype UMetasound::GetArchetype() const
{
	FMetasoundArchetype Archetype;
	static FName MetasoundArchetypeName = TEXT("Generic Metasound");

	Archetype.ArchetypeName = MetasoundArchetypeName;

	return Archetype;
}
