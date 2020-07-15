// Copyright Epic Games, Inc. All Rights Reserved.
#include "Metasound.h"

#include "Backends/JsonStructSerializerBackend.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "StructSerializer.h"

UMetasound::UMetasound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AccessPoint(MakeShared<Metasound::Frontend::FDescriptionAccessPoint>(RootMetasoundDocument))
{
}

FMetasoundClassMetadata UMetasound::GetMetadata()
{
	return RootMetasoundDocument.RootClass.Metadata;
}

// This can be used to update the metadata (name, author, etc) for this metasound.
// @param InMetadata may be updated with any corrections we do to the input metadata.
void UMetasound::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	InMetadata.NodeType = EMetasoundClassType::MetasoundGraph;
	RootMetasoundDocument.RootClass.Metadata = InMetadata;
	MarkPackageDirty();
}

// delete this asset's current metasound document,
// and replace it with InClassDescription.
void UMetasound::SetMetasoundDocument(const FMetasoundDocument& InDocument)
{
	RootMetasoundDocument = InDocument;
}

// returns a weak pointer that can be used to build a TDescriptionPtr
// for direct editing of the FMetasoundClassDescription tree.
// For advance use only, and requires knowledge of Metasound::Frontend::FDescPath syntax.
// For most use cases, use GetGraphHandle() instead.
TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint> UMetasound::GetGraphAccessPoint()
{
	return TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint>(AccessPoint);
}

// Get the handle for the root metasound graph of this asset.
Metasound::Frontend::FGraphHandle UMetasound::GetRootGraphHandle()
{
	using FDescPath = Metasound::Frontend::FDescPath;
	using FGraphHandle = Metasound::Frontend::FGraphHandle;
	using FHandleInitParams = Metasound::Frontend::FHandleInitParams;
	using EFromDocument = Metasound::Frontend::Path::EFromDocument;
	using EFromClass = Metasound::Frontend::Path::EFromClass;

	FDescPath PathToGraph = FDescPath()[EFromDocument::ToRootClass][EFromClass::ToGraph];
	FHandleInitParams InitParams = { GetGraphAccessPoint(), PathToGraph, RootMetasoundDocument.RootClass.Metadata.NodeName, MakeWeakObjectPtr(this) };
	return FGraphHandle(FHandleInitParams::PrivateToken, InitParams);
}

TArray<Metasound::Frontend::FGraphHandle> UMetasound::GetAllSubgraphHandles()
{
	using FDescPath = Metasound::Frontend::FDescPath;
	using FGraphHandle = Metasound::Frontend::FGraphHandle;
	using FHandleInitParams = Metasound::Frontend::FHandleInitParams;
	using EFromDocument = Metasound::Frontend::Path::EFromDocument;
	using EFromClass = Metasound::Frontend::Path::EFromClass;

	TArray<FGraphHandle> OutArray;

	FDescPath RootPathForDependencyGraphs = FDescPath()[EFromDocument::ToDependencies];

	TArray<FMetasoundClassDescription>& DependenciesList = RootMetasoundDocument.Dependencies;
	for (FMetasoundClassDescription& Dependency : DependenciesList)
	{
		const bool bIsSubgraph = Dependency.Metadata.NodeType == EMetasoundClassType::MetasoundGraph && Dependency.Graph.Nodes.Num() > 0;
		if (bIsSubgraph)
		{
			FDescPath SubgraphPath = RootPathForDependencyGraphs[Dependency.UniqueID][EFromClass::ToGraph];
			FHandleInitParams InitParams = { GetGraphAccessPoint(), SubgraphPath, Dependency.Metadata.NodeName, MakeWeakObjectPtr(this) };
			OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams);
		}
	}

	return OutArray;
}

bool UMetasound::ExportToJSON(const FString& InAbsolutePath)
{
	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
	{
		FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize<FMetasoundDocument>(RootMetasoundDocument, Backend);
		FileWriter->Close();

		return true;
	}

	ensureAlwaysMsgf(false, TEXT("Failed to create a filewriter with the given path."));
	return false;
}
