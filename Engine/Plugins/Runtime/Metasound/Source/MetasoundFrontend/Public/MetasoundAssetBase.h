// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundGraph.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UEdGraph;

class METASOUNDFRONTEND_API FMetasoundAssetBase
{
public:
	static const FString FileExtension;

	FMetasoundAssetBase();
	FMetasoundAssetBase(FMetasoundDocument& InDocument);

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
	virtual FMetasoundArchetype GetArchetype() const = 0;

	// Returns the root class metadata
	FMetasoundClassMetadata GetMetadata();

	// Imports data from a JSON string directly
	bool ImportFromJSON(const FString& InJSON);

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSONAsset(const FString& InAbsolutePath);

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle() const;

	// Returns all handles for subgraphs referenced
	TArray<Metasound::Frontend::FGraphHandle> GetAllSubgraphHandles();

	// Overwrites the entire document and fixes it up based on any 
	// inputs or outputs in the archetype that are missing from the graph.
	void SetDocument(const FMetasoundDocument& InDocument);

	// This must be called on UObject::PostLoad, as well as in this asset's UFactory, to fix up the root document based on the most recent version of the archetype.
	void ConformDocumentToArchetype();

protected:
	// Returns private token allowing implementing asset class to use graph/node handle system
	static Metasound::Frontend::FHandleInitParams::EPrivateToken GetPrivateToken()
	{
		return Metasound::Frontend::FHandleInitParams::PrivateToken;
	}

	// Returns document object responsible for serializing asset
	virtual FMetasoundDocument& GetDocument() = 0;

	// Returns document object responsible for serializing asset
	virtual const FMetasoundDocument& GetDocument() const = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual UObject* GetOwningAsset() const = 0;

	// Returns a weak pointer that can be used to build a TDescriptionPtr
	// for direct editing of the FMetasoundClassDescription tree.
	// For advance use only, and requires knowledge of Metasound::Frontend::FDescPath syntax.
	// For most use cases, use GetRootGraphHandle() instead.
	TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint> GetGraphAccessPoint() const;

	TSharedPtr<Metasound::Frontend::FDescriptionAccessPoint> AccessPoint;
};
