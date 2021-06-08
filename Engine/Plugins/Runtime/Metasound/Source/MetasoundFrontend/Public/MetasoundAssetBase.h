// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundGraph.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "UObject/WeakObjectPtrTemplates.h"



class UEdGraph;

/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundFrontendDocuments, control
 * over the FMetasoundFrontendArchetype of the FMetasoundFrontendDocument.  It also enables the UObject
 * to be utilized by a host of other engine tools built to support Metasounds.
 */
class METASOUNDFRONTEND_API FMetasoundAssetBase
{
public:
	static const FString FileExtension;

	FMetasoundAssetBase() = default;
	virtual ~FMetasoundAssetBase() = default;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const = 0;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with this metasound uobject.
	virtual UEdGraph* GetGraph() = 0;
	virtual const UEdGraph* GetGraph() const = 0;
	virtual UEdGraph& GetGraphChecked() = 0;
	virtual const UEdGraph& GetGraphChecked() const = 0;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with this metasound object.
	virtual void SetGraph(UEdGraph* InGraph) = 0;

#endif // WITH_EDITORONLY_DATA

	// Registers the root graph of the given asset with the MetaSound Frontend.
	void RegisterGraphWithFrontend();

	bool CopyDocumentAndInjectReceiveNodes(uint64 InInstanceID, const FMetasoundFrontendDocument& InSourceDoc, FMetasoundFrontendDocument& OutDestDoc) const;

	// Sets/overwrites the root class metadata
	virtual void SetMetadata(FMetasoundFrontendClassMetadata& InMetadata);

	// Returns  a description of the required inputs and outputs for this metasound UClass.
	virtual const FMetasoundFrontendArchetype& GetArchetype() const = 0;

	// Returns true if the archetype is supported by this object.
	virtual bool IsArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const;

	// Returns an array of archetypes preferred for this class.
	virtual const TArray<FMetasoundFrontendArchetype>& GetPreferredArchetypes() const = 0;

	// Returns the preferred archetype for the given document.
	virtual const FMetasoundFrontendArchetype& GetPreferredArchetypes(const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InDefaultArchetype) const;

	// Returns the root class metadata
	FMetasoundFrontendClassMetadata GetMetadata();

	// TODO:
	//virtual void OnMetaSoundDependencyAdded(const FMetasoundFrontendClassMetadata& InMetadata) = 0;
	//virtual void OnMetaSoundDependencyRemoved(const FMetasoundFrontendClassMetadata& InMetadata) = 0;

	// Imports data from a JSON string directly
	bool ImportFromJSON(const FString& InJSON);

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSONAsset(const FString& InAbsolutePath);

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FDocumentHandle GetDocumentHandle();
	Metasound::Frontend::FConstDocumentHandle GetDocumentHandle() const;

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle();
	Metasound::Frontend::FConstGraphHandle GetRootGraphHandle() const;


	// Overwrites the existing document. If the document's archetype is not supported,
	// the FMetasoundAssetBase be while queried for a new one using `GetPreferredArchetype`. If `bForceUpdateArchetype`
	// is true, `GetPreferredArchetype` will be used whether or not the provided document's archetype
	// is supported. 
	void SetDocument(const FMetasoundFrontendDocument& InDocument);

	FMetasoundFrontendDocument& GetDocumentChecked();
	const FMetasoundFrontendDocument& GetDocumentChecked() const;

	// This must be called on UObject::PostLoad, as well as in this asset's UFactory, to fix up the root document based on the most recent version of the archetype.
	void ConformDocumentToArchetype();

	// Calls the outermost package and marks it dirty. 
	bool MarkMetasoundDocumentDirty() const;

protected:
	struct FSendInfoAndVertexName
	{
		Metasound::FMetasoundInstanceTransmitter::FSendInfo SendInfo;
		FString VertexName;
	};

	TArray<FSendInfoAndVertexName> GetSendInfos(uint64 InInstanceID) const;

#if WITH_EDITORONLY_DATA
	FText GetDisplayName(FString&& InTypeName) const;
#endif // WITH_EDITORONLY_DATA

	void UpdateAssetTags(FMetasoundFrontendClassAssetTags& OutTags);

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FDocumentAccessPtr GetDocument() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual UObject* GetOwningAsset() = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual const UObject* GetOwningAsset() const = 0;


private:
	bool GetReceiveNodeMetadataForDataType(const FName& InTypeName, FMetasoundFrontendClassMetadata& OutMetadata) const;
	TArray<FString> GetTransmittableInputVertexNames() const;
	Metasound::FSendAddress CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const;
	Metasound::Frontend::FNodeHandle AddInputPinForSendAddress(const Metasound::FMetasoundInstanceTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const;

	Metasound::Frontend::FNodeRegistryKey RegistryKey;
};
