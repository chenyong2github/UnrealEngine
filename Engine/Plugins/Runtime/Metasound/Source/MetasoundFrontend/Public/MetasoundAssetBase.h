// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundGraph.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UEdGraph;

/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundDocuments, control
 * over the FMetasoundArchetype of the FMetasoundDocument.  It also enables the UObject
 * to be utilized by a host of other engine tools built to support Metasounds.
 *
 * Example Usage:
 *
 * class UMyMetasoundClass : public UObject, public FMetasoundAssetBase
 * {
 *
 *     	UPROPERTY()
 *     	FMetasoundDocument MetasoundDocument;
 *
 *	 	Metasound::Frontend::FAccessPoint MetasoundSourceAccessPoint;
 *
 * #if WITH_EDITORONLY_DATA
 * 		UPROPERTY()
 * 		UEdGraph* Graph;
 * #endif // WITH_EDITORONLY_DATA
 *
 * 	public:
 * 		// FMetasoundAssetBase Interface
 * 		...
 *      // End FMetasoundAssetBase Interface
 * };
 *
 */
class METASOUNDFRONTEND_API FMetasoundAssetBase
{
public:
	static const FString FileExtension;

	FMetasoundAssetBase();

	// Construct an FMetasoundAssetBase with a default Metasound Archetype.
	// @param InDefaultArchetype - Default archetype for a Metasound Document in this class.
	FMetasoundAssetBase(const FMetasoundArchetype& InDefaultArchetype);

	virtual ~FMetasoundAssetBase() = default;

#if WITH_EDITORONLY_DATA

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

	// Sets/overwrites the root class metadata
	virtual void SetMetadata(FMetasoundClassMetadata& InMetadata);

	// Returns  a description of the required inputs and outputs for this metasound UClass.
	const FMetasoundArchetype& GetArchetype() const;

	// Sets/overwrites the document archetype.
	// @param InArchetype - The desired archetype .
	//
	// @return True on success. False if archetype is not supported by this object. 
	bool SetArchetype(const FMetasoundArchetype& InArchetype);

	// Returns an array of archetypes preferred for this class.
	virtual const TArray<FMetasoundArchetype>& GetPreferredArchetypes() const = 0;

	// Returns true if the archetype is supported by this object.
	virtual bool IsArchetypeSupported(const FMetasoundArchetype& InArchetype) const;

	// Returns the preferred archetype for the given document.
	virtual const FMetasoundArchetype& GetPreferredArchetype(const FMetasoundDocument& InDocument) const;

	// Returns the root class metadata
	FMetasoundClassMetadata GetMetadata();

	// Imports data from a JSON string directly
	bool ImportFromJSON(const FString& InJSON);

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSONAsset(const FString& InAbsolutePath);

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle();

	// Returns all handles for subgraphs referenced
	TArray<Metasound::Frontend::FGraphHandle> GetAllSubgraphHandles();

	// Overwrites the existing document. If the document's archetype is not supported,
	// the FMetasoundAssetBase be while queried for a new one using `GetPreferredArchetype`. If `bForceUpdateArchetype`
	// is true, `GetPreferredArchetype` will be used whether or not the provided document's archetype
	// is supported. 
	void SetDocument(const FMetasoundDocument& InDocument, bool bForceUpdateArchetype=false);

	
	FMetasoundDocument& GetDocumentChecked();
	const FMetasoundDocument& GetDocumentChecked() const;

	// This must be called on UObject::PostLoad, as well as in this asset's UFactory, to fix up the root document based on the most recent version of the archetype.
	void ConformDocumentToArchetype();

protected:
	// Returns private token allowing implementing asset class to use graph/node handle system
	static Metasound::Frontend::FHandleInitParams::EPrivateToken GetPrivateToken()
	{
		return Metasound::Frontend::FHandleInitParams::PrivateToken;
	}

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::TAccessPtr<FMetasoundDocument> GetDocument() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::TAccessPtr<const FMetasoundDocument> GetDocument() const = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual UObject* GetOwningAsset() const = 0;

	// Returns a access point that can be used to build a TDescriptionPtr
	// for direct editing of the FMetasoundClassDescription tree.
	// For advance use only, and requires knowledge of Metasound::Frontend::FDescPath syntax.
	// For most use cases, use GetRootGraphHandle() instead.
	Metasound::Frontend::FDescriptionAccessPoint GetGraphAccessPoint();

private:

	FMetasoundArchetype Archetype;
};
