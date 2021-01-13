// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetBase.h"

#include "Algo/AnyOf.h"
#include "HAL/FileManager.h"
#include "IStructSerializerBackend.h"
#include "MetasoundArchetype.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundLog.h"
#include "StructSerializer.h"

const FString FMetasoundAssetBase::FileExtension(TEXT(".metasound"));

FMetasoundAssetBase::FMetasoundAssetBase()
{
}

FMetasoundAssetBase::FMetasoundAssetBase(const FMetasoundFrontendArchetype& InArchetype)
:	Archetype(InArchetype)
{
}

void FMetasoundAssetBase::SetMetadata(FMetasoundFrontendClassMetadata& InMetadata)
{
	FMetasoundFrontendDocument& Doc = GetDocumentChecked();
	Doc.RootGraph.Metadata = InMetadata;

	if (Doc.RootGraph.Metadata.Type != EMetasoundFrontendClassType::Graph)
	{
		UE_LOG(LogMetasound, Display, TEXT("Forcing class type to EMetasoundFrontendClassType::Graph on root graph metadata"));
		Doc.RootGraph.Metadata.Type = EMetasoundFrontendClassType::Graph;
	}
}

const FMetasoundFrontendArchetype& FMetasoundAssetBase::GetArchetype() const
{
	return Archetype;
}

bool FMetasoundAssetBase::SetArchetype(const FMetasoundFrontendArchetype& InArchetype)
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

bool FMetasoundAssetBase::IsArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const
{
	auto IsEqualArchetype = [&](const FMetasoundFrontendArchetype& SupportedArchetype)
	{
		return Metasound::Frontend::IsEqualArchetype(InArchetype, SupportedArchetype);
	};

	return Algo::AnyOf(GetPreferredArchetypes(), IsEqualArchetype);
}

const FMetasoundFrontendArchetype& FMetasoundAssetBase::GetPreferredArchetype(const FMetasoundFrontendDocument& InDocument) const
{
	// Default to archetype provided in case it is supported. 
	if (IsArchetypeSupported(InDocument.Archetype))
	{
		return InDocument.Archetype;
	}

	// If existing archetype is not supported, get the most similar that still supports the documents environment.
	const FMetasoundFrontendArchetype* SimilarArchetype = Metasound::Frontend::FindMostSimilarArchetypeSupportingEnvironment(InDocument, GetPreferredArchetypes());

	if (nullptr != SimilarArchetype)
	{
		return *SimilarArchetype;
	}

	// Nothing found. Return the existing archetype for the FMetasoundAssetBase.
	return GetArchetype();
}

void FMetasoundAssetBase::SetDocument(const FMetasoundFrontendDocument& InDocument, bool bForceUpdateArchetype)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document = InDocument;

	FMetasoundFrontendArchetype NewArch = InDocument.Archetype;

	if (bForceUpdateArchetype || (!IsArchetypeSupported(NewArch)))
	{
		NewArch = GetPreferredArchetype(Document);
	}

	ensure(SetArchetype(NewArch));
}

void FMetasoundAssetBase::ConformDocumentToArchetype()
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document.Archetype = GetArchetype();

	Metasound::Frontend::FMatchRootGraphToArchetype Transform;
	Transform.Transform(GetDocumentHandle());
}

FMetasoundFrontendClassMetadata FMetasoundAssetBase::GetMetadata()
{
	return GetDocumentChecked().RootGraph.Metadata;
}

Metasound::Frontend::FDocumentHandle FMetasoundAssetBase::GetDocumentHandle()
{
	return Metasound::Frontend::IDocumentController::CreateDocumentHandle(GetDocument());
}

Metasound::Frontend::FConstDocumentHandle FMetasoundAssetBase::GetDocumentHandle() const
{
	return Metasound::Frontend::IDocumentController::CreateDocumentHandle(GetDocument());
}

Metasound::Frontend::FGraphHandle FMetasoundAssetBase::GetRootGraphHandle()
{
	return GetDocumentHandle()->GetRootGraph();
}

Metasound::Frontend::FConstGraphHandle FMetasoundAssetBase::GetRootGraphHandle() const
{
	return GetDocumentHandle()->GetRootGraph();
}

bool FMetasoundAssetBase::ImportFromJSON(const FString& InJSON)
{
	Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		return Metasound::Frontend::ImportJSONToMetasound(InJSON, *Document);
	}
	return false;
}

bool FMetasoundAssetBase::ImportFromJSONAsset(const FString& InAbsolutePath)
{
	Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		return Metasound::Frontend::ImportJSONAssetToMetasound(InAbsolutePath, *Document);
	}
	return false;
}

FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked()
{
	Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}

const FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked() const
{
	Metasound::Frontend::TAccessPtr<const FMetasoundFrontendDocument> DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}
