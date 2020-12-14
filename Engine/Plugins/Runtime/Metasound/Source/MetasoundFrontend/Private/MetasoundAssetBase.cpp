// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Algo/AnyOf.h"
#include "HAL/FileManager.h"
#include "IStructSerializerBackend.h"
#include "MetasoundArchetype.h"
#include "MetasoundJsonBackend.h"
#include "StructSerializer.h"

const FString FMetasoundAssetBase::FileExtension(TEXT(".metasound"));

FMetasoundAssetBase::FMetasoundAssetBase()
{
}

FMetasoundAssetBase::FMetasoundAssetBase(const FMetasoundArchetype& InArchetype)
:	Archetype(InArchetype)
{
}

void FMetasoundAssetBase::SetMetadata(FMetasoundClassMetadata& InMetadata)
{
	InMetadata.NodeType = EMetasoundClassType::MetasoundGraph;
	GetDocumentChecked().RootClass.Metadata = InMetadata;
}

const FMetasoundArchetype& FMetasoundAssetBase::GetArchetype() const
{
	return Archetype;
}

bool FMetasoundAssetBase::SetArchetype(const FMetasoundArchetype& InArchetype)
{
	if (IsArchetypeSupported(InArchetype))
	{
		Archetype = InArchetype;
		ConformDocumentToArchetype();

		return true;
	}

	// Archetype was not set because it is not supported for this asset base. 
	return false;
}

bool FMetasoundAssetBase::IsArchetypeSupported(const FMetasoundArchetype& InArchetype) const
{
	auto IsEqualArchetype = [&](const FMetasoundArchetype& SupportedArchetype)
	{
		return Metasound::Frontend::IsEqualArchetype(InArchetype, SupportedArchetype);
	};

	return Algo::AnyOf(GetPreferredArchetypes(), IsEqualArchetype);
}

const FMetasoundArchetype& FMetasoundAssetBase::GetPreferredArchetype(const FMetasoundDocument& InDocument) const
{
	// Default to archetype provided in case it is supported. 
	if (IsArchetypeSupported(InDocument.Archetype))
	{
		return InDocument.Archetype;
	}

	// If existing archetype is not supported, get the most similar that still supports the documents environment.
	const FMetasoundArchetype* SimilarArchetype = Metasound::Frontend::FindMostSimilarArchetypeSupportingEnvironment(InDocument, GetPreferredArchetypes());

	if (nullptr != SimilarArchetype)
	{
		return *SimilarArchetype;
	}

	// Nothing found. Return the existing archetype for the FMetasoundAssetBase.
	return GetArchetype();
}

void FMetasoundAssetBase::SetDocument(const FMetasoundDocument& InDocument, bool bForceUpdateArchetype)
{
	FMetasoundDocument& Document = GetDocumentChecked();
	Document = InDocument;

	FMetasoundArchetype NewArch = InDocument.Archetype;

	if (bForceUpdateArchetype || (!IsArchetypeSupported(NewArch)))
	{
		NewArch = GetPreferredArchetype(Document);
	}

	ensure(SetArchetype(NewArch));
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	FMetasoundDocument& Document = GetDocumentChecked();
	Document.Archetype = GetArchetype();
	GetRootGraphHandle().FixDocumentToMatchArchetype();
}

FMetasoundClassMetadata FMetasoundAssetBase::GetMetadata()
{
	return GetDocumentChecked().RootClass.Metadata;
}

Metasound::Frontend::FDescriptionAccessPoint FMetasoundAssetBase::GetGraphAccessPoint() 
{
	return Metasound::Frontend::FDescriptionAccessPoint(GetDocument());
}

Metasound::Frontend::FGraphHandle FMetasoundAssetBase::GetRootGraphHandle()
{
	using FDescPath = Metasound::Frontend::FDescPath;
	using FGraphHandle = Metasound::Frontend::FGraphHandle;
	using FHandleInitParams = Metasound::Frontend::FHandleInitParams;
	using EFromDocument = Metasound::Frontend::Path::EFromDocument;
	using EFromClass = Metasound::Frontend::Path::EFromClass;

	FDescPath PathToGraph = FDescPath()[EFromDocument::ToRootClass][EFromClass::ToGraph];
	FHandleInitParams InitParams = { GetGraphAccessPoint(), PathToGraph, INDEX_NONE, GetDocumentChecked().RootClass.UniqueID, MakeWeakObjectPtr(GetOwningAsset()) };
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

	TArray<FMetasoundClassDescription>& DependenciesList = GetDocumentChecked().Dependencies;
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
	Metasound::Frontend::TAccessPtr<FMetasoundDocument> Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		return Metasound::Frontend::ImportJSONToMetasound(InJSON, *Document);
	}
	return false;
}

bool FMetasoundAssetBase::ImportFromJSONAsset(const FString& InAbsolutePath)
{
	Metasound::Frontend::TAccessPtr<FMetasoundDocument> Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		return Metasound::Frontend::ImportJSONAssetToMetasound(InAbsolutePath, *Document);
	}
	return false;
}

FMetasoundDocument& FMetasoundAssetBase::GetDocumentChecked()
{
	Metasound::Frontend::TAccessPtr<FMetasoundDocument> DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}

const FMetasoundDocument& FMetasoundAssetBase::GetDocumentChecked() const
{
	Metasound::Frontend::TAccessPtr<const FMetasoundDocument> DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}
