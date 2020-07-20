// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Backends/JsonStructSerializerBackend.h"
#include "IStructSerializerBackend.h"
#include "StructSerializer.h"
#include "HAL/FileManager.h"


void FMetasoundAssetBase::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	InMetadata.NodeType = EMetasoundClassType::MetasoundGraph;
	GetDocument().RootClass.Metadata = InMetadata;
}

FMetasoundClassMetadata FMetasoundAssetBase::GetMetadata()
{
	return GetDocument().RootClass.Metadata;
}

TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint> FMetasoundAssetBase::GetGraphAccessPoint()
{
	if (!AccessPoint.IsValid())
	{
		AccessPoint = MakeShared<Metasound::Frontend::FDescriptionAccessPoint>(GetDocument());
	}

	return TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint>(AccessPoint);
}

Metasound::Frontend::FGraphHandle FMetasoundAssetBase::GetRootGraphHandle()
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

bool FMetasoundAssetBase::ExportToJSON(const FString& InAbsolutePath)
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