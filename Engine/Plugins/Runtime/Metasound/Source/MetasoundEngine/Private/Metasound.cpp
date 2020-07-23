// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"


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

const FText& UMetasound::GetInputToolTip(FString InputName) const
{
	for (const FMetasoundInputDescription& Desc : RootMetasoundDocument.RootClass.Inputs)
	{
		if (Desc.Name == InputName)
		{
			return Desc.ToolTip;
		}
	}

	return FText::GetEmpty();
}

const FText& UMetasound::GetOutputToolTip(FString OutputName) const
{
	for (const FMetasoundOutputDescription& Desc : RootMetasoundDocument.RootClass.Outputs)
	{
		if (Desc.Name == OutputName)
		{
			return Desc.ToolTip;
		}
	}

	return FText::GetEmpty();
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

// delete this asset's current metasound document,
// and replace it with InClassDescription.
void UMetasound::SetMetasoundDocument(const FMetasoundDocument& InDocument)
{
	RootMetasoundDocument = InDocument;
}
