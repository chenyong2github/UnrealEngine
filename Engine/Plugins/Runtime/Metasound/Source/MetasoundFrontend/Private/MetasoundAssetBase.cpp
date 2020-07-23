// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Backends/JsonStructSerializerBackend.h"
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
	FHandleInitParams InitParams = { GetGraphAccessPoint(), PathToGraph, GetDocument().RootClass.Metadata.NodeName, MakeWeakObjectPtr(GetOwningAsset()) };
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
			FHandleInitParams InitParams = { GetGraphAccessPoint(), SubgraphPath, Dependency.Metadata.NodeName, MakeWeakObjectPtr(GetOwningAsset()) };
			OutArray.Emplace(GetPrivateToken(), InitParams);
		}
	}

	return OutArray;
}

bool FMetasoundAssetBase::ImportFromJSON(const FString& InAbsolutePath)
{
	return Metasound::Frontend::ImportJSONToMetasound(InAbsolutePath, GetDocument());
}

bool FMetasoundAssetBase::ExportToJSON(const FString& InAbsolutePath) const
{
	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
	{
		FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize<FMetasoundDocument>(GetDocument(), Backend);
		FileWriter->Close();

		return true;
	}

	ensureAlwaysMsgf(false, TEXT("Failed to create a filewriter with the given path."));
	return false;
}