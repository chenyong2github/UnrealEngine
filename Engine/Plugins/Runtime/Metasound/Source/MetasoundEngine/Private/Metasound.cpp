// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"

UMetaSound::UMetaSound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(FMetasoundFrontendArchetype())
{
}

#if WITH_EDITORONLY_DATA
UEdGraph* UMetaSound::GetGraph()
{
	return Graph;
}

const UEdGraph* UMetaSound::GetGraph() const
{
	return Graph;
}

UEdGraph& UMetaSound::GetGraphChecked()
{
	check(Graph);
	return *Graph;
}

const UEdGraph& UMetaSound::GetGraphChecked() const
{
	check(Graph);
	return *Graph;
}

void UMetaSound::SetGraph(UEdGraph* InGraph)
{
	Graph = InGraph;
}
#endif // WITH_EDITORONLY_DATA

// Returns document object responsible for serializing asset
Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> UMetaSound::GetDocument()
{
	return Metasound::Frontend::MakeAccessPtr(MetasoundDocument.AccessPoint, MetasoundDocument);
}

// Returns document object responsible for serializing asset
Metasound::Frontend::TAccessPtr<const FMetasoundFrontendDocument> UMetaSound::GetDocument() const
{
	return Metasound::Frontend::MakeAccessPtr<const FMetasoundFrontendDocument>(MetasoundDocument.AccessPoint, MetasoundDocument);
}

const TArray<FMetasoundFrontendArchetype>& UMetaSound::GetPreferredMetasoundArchetypes() const
{
	// Not preferred archetypes for a basic UMetaSound.
	static const TArray<FMetasoundFrontendArchetype> Preferred;
	return Preferred;
}

bool UMetaSound::IsMetasoundArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const
{
	// All archetypes are supported.
	return true;
}

const FMetasoundFrontendArchetype& UMetaSound::GetPreferredMetasoundArchetype(const FMetasoundFrontendDocument& InDocument) const
{
	// Prefer to keep original archetype.
	return InDocument.Archetype;
}

