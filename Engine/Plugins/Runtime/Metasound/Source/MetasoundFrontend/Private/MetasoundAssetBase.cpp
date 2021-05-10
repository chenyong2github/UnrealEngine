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
		UE_LOG(LogMetaSound, Display, TEXT("Forcing class type to EMetasoundFrontendClassType::Graph on root graph metadata"));
		Doc.RootGraph.Metadata.Type = EMetasoundFrontendClassType::Graph;
	}

	MarkMetasoundDocumentDirty();
}

const FMetasoundFrontendArchetype& FMetasoundAssetBase::GetMetasoundArchetype() const
{
	return Archetype;
}

bool FMetasoundAssetBase::SetMetasoundArchetype(const FMetasoundFrontendArchetype& InArchetype)
{
	if (IsMetasoundArchetypeSupported(InArchetype))
	{
		Archetype = InArchetype;
		ConformDocumentToMetasoundArchetype();

		return true;
	}

	// Archetype was not set because it is not supported for this asset base. 
	return false;
}

bool FMetasoundAssetBase::IsMetasoundArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const
{
	auto IsEqualArchetype = [&](const FMetasoundFrontendArchetype& SupportedArchetype)
	{
		return Metasound::Frontend::IsEqualArchetype(InArchetype, SupportedArchetype);
	};

	return Algo::AnyOf(GetPreferredMetasoundArchetypes(), IsEqualArchetype);
}

const FMetasoundFrontendArchetype& FMetasoundAssetBase::GetPreferredMetasoundArchetype(const FMetasoundFrontendDocument& InDocument) const
{
	// Default to archetype provided in case it is supported. 
	if (IsMetasoundArchetypeSupported(InDocument.Archetype))
	{
		return InDocument.Archetype;
	}

	// If existing archetype is not supported, get the most similar that still supports the documents environment.
	const FMetasoundFrontendArchetype* SimilarArchetype = Metasound::Frontend::FindMostSimilarArchetypeSupportingEnvironment(InDocument, GetPreferredMetasoundArchetypes());

	if (nullptr != SimilarArchetype)
	{
		return *SimilarArchetype;
	}

	// Nothing found. Return the existing archetype for the FMetasoundAssetBase.
	return GetMetasoundArchetype();
}

void FMetasoundAssetBase::SetDocument(const FMetasoundFrontendDocument& InDocument, bool bForceUpdateArchetype)
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document = InDocument;

	FMetasoundFrontendArchetype NewArch = InDocument.Archetype;

	if (bForceUpdateArchetype || (!IsMetasoundArchetypeSupported(NewArch)))
	{
		NewArch = GetPreferredMetasoundArchetype(Document);
	}

	ensure(SetMetasoundArchetype(NewArch));
}

void FMetasoundAssetBase::ConformDocumentToMetasoundArchetype()
{
	FMetasoundFrontendDocument& Document = GetDocumentChecked();
	Document.Archetype = GetMetasoundArchetype();

	Metasound::Frontend::FMatchRootGraphToArchetype Transform;
	Transform.Transform(GetDocumentHandle());

	MarkMetasoundDocumentDirty();
}

bool FMetasoundAssetBase::MarkMetasoundDocumentDirty() const
{
	if (const UObject* OwningAsset = GetOwningAsset())
	{
		return ensure(OwningAsset->MarkPackageDirty());
	}
	return false;
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
	Metasound::Frontend::FDocumentAccessPtr Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		bool bSuccess = Metasound::Frontend::ImportJSONToMetasound(InJSON, *Document);

		if (bSuccess)
		{
			ensure(MarkMetasoundDocumentDirty());
		}

		return bSuccess;
	}
	return false;
}

bool FMetasoundAssetBase::ImportFromJSONAsset(const FString& InAbsolutePath)
{
	Metasound::Frontend::FDocumentAccessPtr Document = GetDocument();
	if (ensure(Document.IsValid()))
	{
		bool bSuccess = Metasound::Frontend::ImportJSONAssetToMetasound(InAbsolutePath, *Document);

		if (bSuccess)
		{
			ensure(MarkMetasoundDocumentDirty());
		}

		return bSuccess;
	}
	return false;
}

FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked()
{
	Metasound::Frontend::FDocumentAccessPtr DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}

const FMetasoundFrontendDocument& FMetasoundAssetBase::GetDocumentChecked() const
{
	Metasound::Frontend::FConstDocumentAccessPtr DocAccessPtr = GetDocument();

	check(DocAccessPtr.IsValid());
	return *DocAccessPtr;
}

