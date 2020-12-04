// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "MetasoundJsonBackend.h"
#include "HAL/FileManager.h"
#include "IStructSerializerBackend.h"
#include "StructSerializer.h"


const FString FMetasoundAssetBase::FileExtension(TEXT(".metasound"));

FMetasoundAssetBase::FMetasoundAssetBase()
{
}

FMetasoundAssetBase::FMetasoundAssetBase(FMetasoundDocument& InDocument)
	: AccessPoint(MakeShared<Metasound::Frontend::FDescriptionAccessPoint>(InDocument))
{
}

void FMetasoundAssetBase::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	InMetadata.NodeType = EMetasoundClassType::MetasoundGraph;
	GetDocument().RootClass.Metadata = InMetadata;
}

void FMetasoundAssetBase::SetDocument(const FMetasoundDocument& InDocument)
{
	FMetasoundDocument& Document = GetDocument();
	Document = InDocument;

	Document.Archetype = GetArchetype();
	GetRootGraphHandle().FixDocumentToMatchArchetype();
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	FMetasoundDocument& Document = GetDocument();
	Document.Archetype = GetArchetype();
	GetRootGraphHandle().FixDocumentToMatchArchetype();
}

FMetasoundClassMetadata FMetasoundAssetBase::GetMetadata()
{
	return GetDocument().RootClass.Metadata;
}

TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint> FMetasoundAssetBase::GetGraphAccessPoint() const
{
	return TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint>(AccessPoint);
}

Metasound::Frontend::FGraphHandle FMetasoundAssetBase::GetRootGraphHandle() const
{
	using FDescPath = Metasound::Frontend::FDescPath;
	using FGraphHandle = Metasound::Frontend::FGraphHandle;
	using FHandleInitParams = Metasound::Frontend::FHandleInitParams;
	using EFromDocument = Metasound::Frontend::Path::EFromDocument;
	using EFromClass = Metasound::Frontend::Path::EFromClass;

	FDescPath PathToGraph = FDescPath()[EFromDocument::ToRootClass][EFromClass::ToGraph];
	FHandleInitParams InitParams = { GetGraphAccessPoint(), PathToGraph, INDEX_NONE, GetDocument().RootClass.UniqueID, MakeWeakObjectPtr(GetOwningAsset()) };
	return FGraphHandle(GetPrivateToken(), InitParams);
}

TArray<Metasound::Frontend::FGraphHandle> FMetasoundAssetBase::GetAllSubgraphHandles()
{
	using FDescPath = Metasound::Frontend::FDescPath;
	using FGraphHandle = Metasound::Frontend::FGraphHandle;
	using FHandleInitParams = Metasound::Frontend::FHandleInitParams;
	using EFromDocument = Metasound::Frontend::Path::EFromDocument;
	using EFromClass = Metasound::Frontend::Path::EFromClass;

	TArray<FGraphHandle> OutArray;

	FDescPath RootPathForDependencyGraphs = FDescPath()[EFromDocument::ToDependencies];

	TArray<FMetasoundClassDescription>& DependenciesList = GetDocument().Dependencies;
	for (FMetasoundClassDescription& Dependency : DependenciesList)
	{
		const bool bIsSubgraph = Dependency.Metadata.NodeType == EMetasoundClassType::MetasoundGraph && Dependency.Graph.Nodes.Num() > 0;
		if (bIsSubgraph)
		{
			FDescPath SubgraphPath = RootPathForDependencyGraphs[Dependency.UniqueID][EFromClass::ToGraph];
			FHandleInitParams InitParams = { GetGraphAccessPoint(), SubgraphPath, INDEX_NONE, Dependency.UniqueID, MakeWeakObjectPtr(GetOwningAsset()) };
			OutArray.Emplace(GetPrivateToken(), InitParams);
		}
	}

	return OutArray;
}

bool FMetasoundAssetBase::ImportFromJSON(const FString& InJSON)
{
	return Metasound::Frontend::ImportJSONToMetasound(InJSON, GetDocument());
}

bool FMetasoundAssetBase::ImportFromJSONAsset(const FString& InAbsolutePath)
{
	return Metasound::Frontend::ImportJSONAssetToMetasound(InAbsolutePath, GetDocument());
}
