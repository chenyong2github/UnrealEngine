// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"

UMetasound::UMetasound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(FMetasoundArchetype())
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

// Returns document object responsible for serializing asset
Metasound::Frontend::TAccessPtr<FMetasoundDocument> UMetasound::GetDocument()
{
	return Metasound::Frontend::MakeAccessPtr(MetasoundDocumentAccessPoint, MetasoundDocument);
}

// Returns document object responsible for serializing asset
Metasound::Frontend::TAccessPtr<const FMetasoundDocument> UMetasound::GetDocument() const
{
	return Metasound::Frontend::MakeAccessPtr<const FMetasoundDocument>(MetasoundDocumentAccessPoint, MetasoundDocument);
}

const TArray<FMetasoundArchetype>& UMetasound::GetPreferredArchetypes() const
{
	// Not preferred archetypes for a basic UMetasound.
	static const TArray<FMetasoundArchetype> Preferred;
	return Preferred;
}

bool UMetasound::IsArchetypeSupported(const FMetasoundArchetype& InArchetype) const
{
	// All archetypes are supported.
	return true;
}

const FMetasoundArchetype& UMetasound::GetPreferredArchetype(const FMetasoundDocument& InDocument) const
{
	// Prefer to keep original archetype.
	return InDocument.Archetype;
}

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

