// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"

UMetasound::UMetasound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(FMetasoundFrontendArchetype())
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
Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> UMetasound::GetDocument()
{
	return Metasound::Frontend::MakeAccessPtr(MetasoundDocument.AccessPoint, MetasoundDocument);
}

// Returns document object responsible for serializing asset
Metasound::Frontend::TAccessPtr<const FMetasoundFrontendDocument> UMetasound::GetDocument() const
{
	return Metasound::Frontend::MakeAccessPtr<const FMetasoundFrontendDocument>(MetasoundDocument.AccessPoint, MetasoundDocument);
}

const TArray<FMetasoundFrontendArchetype>& UMetasound::GetPreferredMetasoundArchetypes() const
{
	// Not preferred archetypes for a basic UMetasound.
	static const TArray<FMetasoundFrontendArchetype> Preferred;
	return Preferred;
}

bool UMetasound::IsMetasoundArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const
{
	// All archetypes are supported.
	return true;
}

const FMetasoundFrontendArchetype& UMetasound::GetPreferredMetasoundArchetype(const FMetasoundFrontendDocument& InDocument) const
{
	// Prefer to keep original archetype.
	return InDocument.Archetype;
}

// This can be used to update the metadata (name, author, etc) for this metasound.
// @param InMetadata may be updated with any corrections we do to the input metadata.
void UMetasound::SetMetadata(FMetasoundFrontendClassMetadata& InMetadata)
{
	FMetasoundAssetBase::SetMetadata(InMetadata);
	MarkPackageDirty();
}

void UMetasound::PostLoad()
{
	ConformDocumentToMetasoundArchetype();
	Super::PostLoad();
}

