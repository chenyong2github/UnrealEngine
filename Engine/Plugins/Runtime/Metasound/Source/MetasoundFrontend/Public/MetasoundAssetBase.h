// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundGraph.h"
#include "MetasoundLog.h"
#include "UObject/WeakObjectPtrTemplates.h"



class UEdGraph;

/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundFrontendDocuments, control
 * over the FMetasoundFrontendArchetype of the FMetasoundFrontendDocument.  It also enables the UObject
 * to be utilized by a host of other engine tools built to support Metasounds.
 *
 * Example Usage:
 *
 * class UMyMetasoundClass : public UObject, public FMetasoundAssetBase
 * {
 *
 *     	UPROPERTY()
 *     	FMetasoundFrontendDocument MetasoundDocument;
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
	FMetasoundAssetBase(const FMetasoundFrontendArchetype& InDefaultArchetype);

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
	virtual void SetMetadata(FMetasoundFrontendClassMetadata& InMetadata);

	// Returns  a description of the required inputs and outputs for this metasound UClass.
	const FMetasoundFrontendArchetype& GetMetasoundArchetype() const;

	// Sets/overwrites the document archetype.
	// @param InArchetype - The desired archetype .
	//
	// @return True on success. False if archetype is not supported by this object. 
	bool SetMetasoundArchetype(const FMetasoundFrontendArchetype& InArchetype);

	// Returns an array of archetypes preferred for this class.
	virtual const TArray<FMetasoundFrontendArchetype>& GetPreferredMetasoundArchetypes() const = 0;

	// Returns true if the archetype is supported by this object.
	virtual bool IsMetasoundArchetypeSupported(const FMetasoundFrontendArchetype& InArchetype) const;

	// Returns the preferred archetype for the given document.
	virtual const FMetasoundFrontendArchetype& GetPreferredMetasoundArchetype(const FMetasoundFrontendDocument& InDocument) const;

	// Returns the root class metadata
	FMetasoundFrontendClassMetadata GetMetadata();

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
	void SetDocument(const FMetasoundFrontendDocument& InDocument, bool bForceUpdateArchetype=false);

	
	FMetasoundFrontendDocument& GetDocumentChecked();
	const FMetasoundFrontendDocument& GetDocumentChecked() const;

	// This must be called on UObject::PostLoad, as well as in this asset's UFactory, to fix up the root document based on the most recent version of the archetype.
	void ConformDocumentToMetasoundArchetype();

	// Calls the outermost package and marks it dirty. 
	bool MarkMetasoundDocumentDirty() const;

protected:

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FDocumentAccessPtr GetDocument() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual UObject* GetOwningAsset() = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual const UObject* GetOwningAsset() const = 0;

private:

	FMetasoundFrontendArchetype Archetype;
};
