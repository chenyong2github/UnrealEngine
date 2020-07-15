// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"

#include "Metasound.generated.h"


/**
 * This asset type is used for Metasound assets that can only be used as nodes in other Metasound graphs.
 * Because of this, they can have any inputs or outputs they need.
 */
UCLASS(hidecategories = object, BlueprintType, MinimalAPI)
class UMetasound : public UObject
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	FMetasoundDocument RootMetasoundDocument;

	TSharedPtr<Metasound::Frontend::FDescriptionAccessPoint> AccessPoint;

public:
	UMetasound(const FObjectInitializer& ObjectInitializer);

	FMetasoundClassMetadata GetMetadata();

	// This can be used to update the metadata (name, author, etc) for this metasound.
	// @param InMetadata may be updated with any corrections we do to the input metadata.
	void SetMetadata(FMetasoundClassMetadata& InMetadata);

	// delete this asset's current metasound document,
	// and replace it with InClassDescription.
	void SetMetasoundDocument(const FMetasoundDocument& InDocument);

	// returns a weak pointer that can be used to build a TDescriptionPtr
	// for direct editing of the FMetasoundClassDescription tree.
	// For advance use only, and requires knowledge of Metasound::Frontend::FDescPath syntax.
	// For most use cases, use GetGraphHandle() instead.
	TWeakPtr<Metasound::Frontend::FDescriptionAccessPoint> GetGraphAccessPoint();

	// Get the handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle();

	TArray<Metasound::Frontend::FGraphHandle> GetAllSubgraphHandles();

	bool ExportToJSON(const FString& InAbsolutePath);
};