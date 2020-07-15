// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#include "Metasound.generated.h"


/**
 * This asset type is used for Metasound assets that can only be used as nodes in other Metasound graphs.
 * Because of this, they can have any inputs or outputs they need.
 */
UCLASS(hidecategories = object, BlueprintType)
class METASOUNDENGINE_API UMetasound : public UObject
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	FMetasoundDocument RootMetasoundDocument;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UEdGraph* Graph;
#endif // WITH_EDITORONLY_DATA

	TSharedPtr<Metasound::Frontend::FDescriptionAccessPoint> AccessPoint;

public:
	UMetasound(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITORONLY_DATA

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetasound.
	UEdGraph* GetGraph();
	UEdGraph& GetGraphChecked();

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetasound.
	void SetGraph(UEdGraph* InGraph);
#endif // WITH_EDITORONLY_DATA

	FMetasoundClassMetadata GetMetadata();

	// Updates the Metasound's metadata (name, author, etc).
	// @param InMetadata Metadata containing corrections to the class metadata.
	void SetMetadata(FMetasoundClassMetadata& InMetadata);

	// Deletes Metasound's current metasound document, and replaces it with InClassDescription.
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