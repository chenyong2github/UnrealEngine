// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundGraph.h"
#include "UObject/WeakObjectPtrTemplates.h"


class METASOUNDFRONTEND_API FMetasoundAssetBase
{
public:
	static const FString FileExtension;

	FMetasoundAssetBase();
	FMetasoundAssetBase(FMetasoundDocument& InDocument);

	virtual ~FMetasoundAssetBase() = default;

	// Sets/overwrites the root class metadata
	virtual void SetMetadata(FMetasoundClassMetadata& InMetadata);

	// Returns the root class metadata
	FMetasoundClassMetadata GetMetadata();

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSON(const FString& InAbsolutePath);

	// Exports the asset to JSON file at provided path
	bool ExportToJSON(const FString& InAbsolutePath) const;

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle() const;

	// Returns all handles for subgraphs referenced
	TArray<Metasound::Frontend::FGraphHandle> GetAllSubgraphHandles();

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
